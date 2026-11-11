// Nirvana Desktop - nirvana-shader: a full-screen GLSL effect over the desktop
// (CRT, amber monitor, VHS...).
//
// A small X11 compositor. All top-level windows are redirected off screen
// (Composite, manual mode), composed with XRender into one buffer the way
// xcompmgr does it, and that buffer is drawn through a fragment shader into the
// composite overlay window with OpenGL (GLX_EXT_texture_from_pixmap; without the
// extension the buffer is copied with XGetImage).
//
// Robustness, because a compositor that dies takes the picture with it:
//  - windows are read through window pictures (IncludeInferiors), not through
//    named pixmaps bound to GL textures - a resized, unmapped or destroyed
//    window never leaves a stale pixmap behind (the usual cause of crashes),
//  - only one GL texture exists (the composed buffer), created once per screen size,
//  - X errors are counted and logged, never fatal,
//  - a shader that does not compile leaves the previous one (or a plain copy),
//  - when the program ends in any way, the X server shows the windows directly again.
//
//   nirvana-shader [NAME|FILE]   run with that shader (default: from shader.conf)
//   nirvana-shader --session     at session start: run only when a shader is chosen
//   nirvana-shader --list        list the shaders (name and file)
//   nirvana-shader --stop        stop the running one
//
// The shader is chosen in Preferences > Shader (~/.config/nirvana/shader.conf,
// "shader=NAME"). A running nirvana-shader follows the _NIRVANA_SHADER property
// on the root window: another name switches the shader, "none" ends it.
//
// Shaders are fragment shaders in GLSL 1.20 (shaders/*.frag). Before the file
// comes a prelude with:
//   uniform vec2  u_resolution;   screen size in pixels
//   uniform float u_time;         seconds since the start (a shader using it is animated)
//   varying vec2  v_texcoord;     0..1, (0,0) in the bottom left corner
//   vec4 screen(vec2 uv);         the desktop at uv (same orientation as v_texcoord)
// The result goes to gl_FragColor.
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <map>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

Display *dpy;
int scr;
Window root;
int root_w, root_h;
Atom A_SHADER, A_CM;
int damage_event, damage_error, shape_event = -1;
volatile sig_atomic_t g_quit = 0;
int g_xerrors = 0;

void log(const char *fmt, const std::string &a = "", const std::string &b = "") {
  fprintf(stderr, "nirvana-shader: ");
  fprintf(stderr, fmt, a.c_str(), b.c_str());
  fputc('\n', stderr);
}

int on_xerror(Display *, XErrorEvent *e) {
  // windows disappear between the event and our request - that is normal; only count
  if (++g_xerrors <= 20 && e->error_code != BadWindow && e->error_code != BadDrawable && e->error_code != BadDamage)
    fprintf(stderr, "nirvana-shader: X error %d (request %d.%d)\n", e->error_code, e->request_code, e->minor_code);
  return 0;
}

// ---------------------------------------------------------------------------
// Files: config and shader directories
// ---------------------------------------------------------------------------
std::string home() { const char *h = getenv("HOME"); return h ? h : "/"; }

std::string config_file() {
  const char *x = getenv("XDG_CONFIG_HOME");
  return (x && *x ? std::string(x) : home() + "/.config") + "/nirvana/shader.conf";
}

std::string configured_shader() {
  std::ifstream in(config_file());
  std::string l;
  while (std::getline(in, l))
    if (!l.compare(0, 7, "shader=")) return l.substr(7);
  return "none";
}

std::string exe_dir() {
  char buf[4096];
  ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  if (n <= 0) return "";
  buf[n] = 0;
  std::string p = buf;
  return p.substr(0, p.rfind('/'));
}

std::vector<std::string> shader_dirs() {
  std::vector<std::string> d;
  const char *x = getenv("XDG_DATA_HOME");
  d.push_back((x && *x ? std::string(x) : home() + "/.local/share") + "/nirvana/shaders");
  std::string e = exe_dir();
  if (!e.empty()) {
    d.push_back(e + "/../share/nirvana/shaders");   // installed in a prefix
    d.push_back(e + "/../shaders");                // running from the build/ folder
  }
  d.push_back("/usr/local/share/nirvana/shaders");
  d.push_back("/usr/share/nirvana/shaders");
  return d;
}

std::string shader_path(const std::string &name) {
  if (name.find('/') != std::string::npos) return name;   // a file
  for (auto &d : shader_dirs()) {
    std::string p = d + "/" + name + ".frag";
    if (!access(p.c_str(), R_OK)) return p;
  }
  return "";
}

std::string read_file(const std::string &p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------
struct Win {
  Window id;
  int x, y, w, h, bw;
  bool mapped = false, input_only = false, argb = false;
  Picture pict = None;
  Damage damage = None;
};
std::vector<Win> wins;   // stacking order: bottom first
bool g_dirty = true;
Window g_overlay = None, g_glwin = None, g_owner = None;

Win *find(Window id) {
  for (auto &w : wins) if (w.id == id) return &w;
  return nullptr;
}

void free_win(Win &w) {
  if (w.pict) { XRenderFreePicture(dpy, w.pict); w.pict = None; }
  if (w.damage) { XDamageDestroy(dpy, w.damage); w.damage = None; }
}

void track_damage(Win &w) {
  if (!w.damage && w.mapped && !w.input_only) w.damage = XDamageCreate(dpy, w.id, XDamageReportNonEmpty);
}

void add(Window id) {   // a new window goes on top
  if (id == g_overlay || id == g_glwin || id == g_owner || find(id)) return;
  XWindowAttributes wa;
  if (!XGetWindowAttributes(dpy, id, &wa)) return;
  Win w;
  w.id = id;
  w.x = wa.x; w.y = wa.y; w.w = wa.width; w.h = wa.height; w.bw = wa.border_width;
  w.input_only = wa.c_class == InputOnly;
  w.mapped = wa.map_state == IsViewable;
  if (!w.input_only) {
    XRenderPictFormat *f = XRenderFindVisualFormat(dpy, wa.visual);
    w.argb = f && f->type == PictTypeDirect && f->direct.alphaMask;
    if (shape_event >= 0) XShapeSelectInput(dpy, id, ShapeNotifyMask);
  }
  track_damage(w);
  wins.push_back(w);
  g_dirty = true;
}

// destroyed: the server has already freed the window's picture and damage with it
void remove(Window id, bool destroyed) {
  for (size_t i = 0; i < wins.size(); i++)
    if (wins[i].id == id) {
      if (!destroyed) free_win(wins[i]);
      wins.erase(wins.begin() + (long)i);
      g_dirty = true;
      return;
    }
}

void restack(Window id, Window above) {   // place id directly above "above" (None: at the bottom)
  auto it = std::find_if(wins.begin(), wins.end(), [&](const Win &w) { return w.id == id; });
  if (it == wins.end()) return;
  Win w = *it;
  wins.erase(it);
  if (above == None) {
    wins.insert(wins.begin(), w);
  } else {
    auto a = std::find_if(wins.begin(), wins.end(), [&](const Win &x) { return x.id == above; });
    wins.insert(a == wins.end() ? wins.end() : a + 1, w);
  }
  g_dirty = true;
}

// ---------------------------------------------------------------------------
// Composing with XRender into the back buffer
// ---------------------------------------------------------------------------
Pixmap g_back = None;
Picture g_back_pict = None;

void create_back() {
  if (g_back_pict) XRenderFreePicture(dpy, g_back_pict);
  if (g_back) XFreePixmap(dpy, g_back);
  g_back = XCreatePixmap(dpy, root, root_w, root_h, 24);
  g_back_pict = XRenderCreatePicture(dpy, g_back, XRenderFindStandardFormat(dpy, PictStandardRGB24), 0, nullptr);
}

void compose() {
  XRenderColor bg = {0x2f00, 0x3700, 0x5400, 0xffff};
  XRenderFillRectangle(dpy, PictOpSrc, g_back_pict, &bg, 0, 0, root_w, root_h);
  for (auto &w : wins) {
    if (!w.mapped || w.input_only) continue;
    if (w.x >= root_w || w.y >= root_h || w.x + w.w + 2 * w.bw <= 0 || w.y + w.h + 2 * w.bw <= 0) continue;
    if (!w.pict) {
      XWindowAttributes wa;
      if (!XGetWindowAttributes(dpy, w.id, &wa)) continue;
      XRenderPictFormat *f = XRenderFindVisualFormat(dpy, wa.visual);
      if (!f) continue;
      XRenderPictureAttributes pa;
      pa.subwindow_mode = IncludeInferiors;
      w.pict = XRenderCreatePicture(dpy, w.id, f, CPSubwindowMode, &pa);
    }
    // the window's shape (round corners, the outline animations of Insight) as the clip
    XserverRegion clip = XFixesCreateRegionFromWindow(dpy, w.id, WindowRegionBounding);
    XFixesTranslateRegion(dpy, clip, w.x + w.bw, w.y + w.bw);
    XFixesSetPictureClipRegion(dpy, g_back_pict, 0, 0, clip);
    XRenderComposite(dpy, w.argb ? PictOpOver : PictOpSrc, w.pict, None, g_back_pict, 0, 0, 0, 0,
                     w.x + w.bw, w.y + w.bw, w.w, w.h);
    XFixesSetPictureClipRegion(dpy, g_back_pict, 0, 0, None);
    XFixesDestroyRegion(dpy, clip);
  }
}

// ---------------------------------------------------------------------------
// OpenGL
// ---------------------------------------------------------------------------
GLXContext g_ctx;
GLXFBConfig g_pixcfg = nullptr;
GLXPixmap g_glxpix = None;
bool g_tfp = false, g_y_inverted = true;
GLuint g_tex = 0, g_prog = 0;
GLint u_res = -1, u_time = -1, u_tex = -1, u_flip = -1;
bool g_animated = false;
std::string g_current;   // the shader in use (name or file)
PFNGLXBINDTEXIMAGEEXTPROC glXBindTexImage;
PFNGLXRELEASETEXIMAGEEXTPROC glXReleaseTexImage;

const char *VERT =
    "#version 120\n"
    "attribute vec2 a_pos;\n"
    "varying vec2 v_texcoord;\n"
    "void main() { v_texcoord = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";

const char *PRELUDE =
    "#version 120\n"
    "uniform sampler2D u_texture;\n"
    "uniform vec2 u_resolution;\n"
    "uniform float u_time;\n"
    "uniform float u_flip;\n"
    "varying vec2 v_texcoord;\n"
    "vec4 screen(vec2 uv) {\n"
    "  uv = clamp(uv, vec2(0.0), vec2(1.0));\n"
    "  if (u_flip > 0.5) uv.y = 1.0 - uv.y;\n"
    "  return texture2D(u_texture, uv);\n"
    "}\n"
    "#line 1\n";

const char *PASSTHROUGH = "void main() { gl_FragColor = vec4(screen(v_texcoord).rgb, 1.0); }\n";

GLuint compile(GLenum type, const std::string &src, const std::string &name) {
  GLuint s = glCreateShader(type);
  const char *p = src.c_str();
  glShaderSource(s, 1, &p, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char logbuf[2048] = "";
    glGetShaderInfoLog(s, sizeof logbuf, nullptr, logbuf);
    log("%s: %s", name, logbuf);
    glDeleteShader(s);
    return 0;
  }
  return s;
}

bool load_program(const std::string &frag_src, const std::string &name) {
  std::string body = frag_src;
  if (body.compare(0, 8, "#version") == 0) body = body.substr(body.find('\n') + 1);   // the prelude has it
  GLuint vs = compile(GL_VERTEX_SHADER, VERT, "vertex");
  GLuint fs = compile(GL_FRAGMENT_SHADER, std::string(PRELUDE) + body, name);
  if (!vs || !fs) { if (vs) glDeleteShader(vs); if (fs) glDeleteShader(fs); return false; }
  GLuint p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glBindAttribLocation(p, 0, "a_pos");
  glLinkProgram(p);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    char logbuf[2048] = "";
    glGetProgramInfoLog(p, sizeof logbuf, nullptr, logbuf);
    log("%s: %s", name, logbuf);
    glDeleteProgram(p);
    return false;
  }
  if (g_prog) glDeleteProgram(g_prog);
  g_prog = p;
  u_res = glGetUniformLocation(p, "u_resolution");
  u_time = glGetUniformLocation(p, "u_time");
  u_tex = glGetUniformLocation(p, "u_texture");
  u_flip = glGetUniformLocation(p, "u_flip");
  g_animated = u_time >= 0;   // the compiler drops u_time when the shader does not use it
  g_dirty = true;
  return true;
}

bool use_shader(const std::string &name) {
  g_current = name;
  std::string path = shader_path(name);
  if (path.empty()) { log("no such shader: %s", name); return false; }
  std::string src = read_file(path);
  if (src.empty()) { log("cannot read %s", path); return false; }
  return load_program(src, name);
}

bool pick_pixmap_config() {
  int n = 0;
  GLXFBConfig *cfgs = glXGetFBConfigs(dpy, scr, &n);
  for (int i = 0; i < n && !g_pixcfg; i++) {
    int v = 0;
    XVisualInfo *vi = glXGetVisualFromFBConfig(dpy, cfgs[i]);
    if (!vi) continue;
    int depth = vi->depth;
    XFree(vi);
    if (depth != 24) continue;
    glXGetFBConfigAttrib(dpy, cfgs[i], GLX_DRAWABLE_TYPE, &v);
    if (!(v & GLX_PIXMAP_BIT)) continue;
    glXGetFBConfigAttrib(dpy, cfgs[i], GLX_BIND_TO_TEXTURE_RGB_EXT, &v);
    if (!v) continue;
    glXGetFBConfigAttrib(dpy, cfgs[i], GLX_BIND_TO_TEXTURE_TARGETS_EXT, &v);
    if (!(v & GLX_TEXTURE_2D_BIT_EXT)) continue;
    glXGetFBConfigAttrib(dpy, cfgs[i], GLX_Y_INVERTED_EXT, &v);
    g_y_inverted = v != 0;
    g_pixcfg = cfgs[i];
  }
  if (cfgs) XFree(cfgs);
  return g_pixcfg != nullptr;
}

void create_texture() {
  if (g_glxpix) { glXDestroyPixmap(dpy, g_glxpix); g_glxpix = None; }
  if (!g_tex) glGenTextures(1, &g_tex);
  glBindTexture(GL_TEXTURE_2D, g_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (g_tfp) {
    const int attrs[] = {GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT, GLX_TEXTURE_FORMAT_EXT, GLX_TEXTURE_FORMAT_RGB_EXT,
                         None};
    g_glxpix = glXCreatePixmap(dpy, g_pixcfg, g_back, attrs);
  } else {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, root_w, root_h, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
  }
}

void upload() {
  glBindTexture(GL_TEXTURE_2D, g_tex);
  if (g_tfp) {
    glXReleaseTexImage(dpy, g_glxpix, GLX_FRONT_LEFT_EXT);
    glXBindTexImage(dpy, g_glxpix, GLX_FRONT_LEFT_EXT, nullptr);
  } else {
    XImage *im = XGetImage(dpy, g_back, 0, 0, root_w, root_h, AllPlanes, ZPixmap);
    if (!im) return;
    if (im->bits_per_pixel == 32) {
      glPixelStorei(GL_UNPACK_ROW_LENGTH, im->bytes_per_line / 4);
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, root_w, root_h, GL_BGRA, GL_UNSIGNED_BYTE, im->data);
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    }
    XDestroyImage(im);
  }
}

double now() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return duration<double>(steady_clock::now() - t0).count();
}

void render() {
  if (g_dirty) { compose(); g_dirty = false; }
  glXWaitX();   // the X server finished composing before GL reads the buffer
  upload();
  glViewport(0, 0, root_w, root_h);
  glUseProgram(g_prog);
  if (u_res >= 0) glUniform2f(u_res, (float)root_w, (float)root_h);
  if (u_time >= 0) glUniform1f(u_time, (float)now());
  if (u_tex >= 0) glUniform1i(u_tex, 0);
  // a texture from a pixmap usually has the first row at the top ("y inverted")
  if (u_flip >= 0) glUniform1f(u_flip, (g_tfp ? g_y_inverted : true) ? 1.0f : 0.0f);
  static const GLfloat quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glDisableVertexAttribArray(0);
  glXSwapBuffers(dpy, g_glwin);
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void input_transparent(Window w) {
  XserverRegion empty = XFixesCreateRegion(dpy, nullptr, 0);
  XFixesSetWindowShapeRegion(dpy, w, ShapeInput, 0, 0, empty);
  XFixesDestroyRegion(dpy, empty);
}

std::string cm_name() { return "_NET_WM_CM_S" + std::to_string(scr); }

std::string get_shader_property() {
  Atom type; int fmt; unsigned long n = 0, after; unsigned char *data = nullptr;
  std::string s;
  if (XGetWindowProperty(dpy, root, A_SHADER, 0, 256, False, AnyPropertyType, &type, &fmt, &n, &after, &data) ==
          Success && data) {
    s.assign((char *)data, n);
    XFree(data);
  }
  return s;
}

void set_shader_property(const std::string &name) {
  XChangeProperty(dpy, root, A_SHADER, XA_STRING, 8, PropModeReplace, (const unsigned char *)name.data(),
                  (int)name.size());
  XFlush(dpy);
}

bool setup(const std::string &shader) {
  int ev, err, maj = 0, min = 0;
  if (!XCompositeQueryExtension(dpy, &ev, &err) || (XCompositeQueryVersion(dpy, &maj, &min), maj == 0 && min < 2)) {
    log("the X server has no Composite extension (0.2+)");
    return false;
  }
  if (!XDamageQueryExtension(dpy, &damage_event, &damage_error)) { log("no Damage extension"); return false; }
  int fe, ferr;
  if (!XFixesQueryExtension(dpy, &fe, &ferr)) { log("no XFixes extension"); return false; }
  int re, rerr;
  if (!XRenderQueryExtension(dpy, &re, &rerr)) { log("no Render extension"); return false; }
  int se, serr;
  if (XShapeQueryExtension(dpy, &se, &serr)) shape_event = se;
  if (!glXQueryExtension(dpy, &ev, &err)) { log("no GLX (OpenGL) - install the Mesa drivers"); return false; }

  // one compositor per screen: the _NET_WM_CM_Sn selection
  A_CM = XInternAtom(dpy, cm_name().c_str(), False);
  if (XGetSelectionOwner(dpy, A_CM) != None) {
    log("another compositor is running - switching its shader to %s", shader);
    set_shader_property(shader);
    return false;
  }
  g_owner = XCreateSimpleWindow(dpy, root, -10, -10, 1, 1, 0, 0, 0);
  XClassHint ch = {(char *)"nirvana-shader", (char *)"Nirvana-Shader"};   // the Shader panel recognizes us by it
  Xutf8SetWMProperties(dpy, g_owner, "nirvana-shader", "nirvana-shader", nullptr, 0, nullptr, nullptr, &ch);
  XSetSelectionOwner(dpy, A_CM, g_owner, CurrentTime);

  // GL: a double-buffered window inside the overlay
  int attrs[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, None};
  XVisualInfo *vi = glXChooseVisual(dpy, scr, attrs);
  if (!vi) { log("no suitable OpenGL visual"); return false; }
  g_ctx = glXCreateContext(dpy, vi, nullptr, True);
  if (!g_ctx) { log("cannot create an OpenGL context"); XFree(vi); return false; }

  const char *exts = glXQueryExtensionsString(dpy, scr);
  glXBindTexImage = (PFNGLXBINDTEXIMAGEEXTPROC)glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
  glXReleaseTexImage = (PFNGLXRELEASETEXIMAGEEXTPROC)glXGetProcAddress((const GLubyte *)"glXReleaseTexImageEXT");
  g_tfp = exts && strstr(exts, "GLX_EXT_texture_from_pixmap") && glXBindTexImage && glXReleaseTexImage &&
          pick_pixmap_config() && !getenv("NIRVANA_SHADER_NO_TFP");

  XCompositeRedirectSubwindows(dpy, root, CompositeRedirectManual);
  g_overlay = XCompositeGetOverlayWindow(dpy, root);
  input_transparent(g_overlay);
  XSetWindowAttributes swa;
  swa.colormap = XCreateColormap(dpy, root, vi->visual, AllocNone);
  swa.border_pixel = 0;
  swa.background_pixmap = None;
  g_glwin = XCreateWindow(dpy, g_overlay, 0, 0, root_w, root_h, 0, vi->depth, InputOutput, vi->visual,
                          CWColormap | CWBorderPixel | CWBackPixmap, &swa);
  XFree(vi);
  input_transparent(g_glwin);
  XMapWindow(dpy, g_glwin);
  glXMakeCurrent(dpy, g_glwin, g_ctx);
  // vsync where the driver offers it
  if (auto swap = (int (*)(unsigned))glXGetProcAddress((const GLubyte *)"glXSwapIntervalMESA")) swap(1);

  create_back();
  create_texture();
  if (!use_shader(shader)) load_program(PASSTHROUGH, "passthrough");

  // windows already on the screen, bottom first
  XGrabServer(dpy);
  XSelectInput(dpy, root, SubstructureNotifyMask | StructureNotifyMask | PropertyChangeMask);
  Window r, p, *kids = nullptr;
  unsigned n = 0;
  if (XQueryTree(dpy, root, &r, &p, &kids, &n)) {
    for (unsigned i = 0; i < n; i++) add(kids[i]);
    if (kids) XFree(kids);
  }
  XUngrabServer(dpy);
  set_shader_property(shader);
  return true;
}

void cleanup() {
  for (auto &w : wins) free_win(w);
  wins.clear();
  if (g_glxpix) glXDestroyPixmap(dpy, g_glxpix);
  glXMakeCurrent(dpy, None, nullptr);
  if (g_ctx) glXDestroyContext(dpy, g_ctx);
  if (g_glwin) XDestroyWindow(dpy, g_glwin);
  if (g_overlay) XCompositeReleaseOverlayWindow(dpy, root);
  XCompositeUnredirectSubwindows(dpy, root, CompositeRedirectManual);
  if (g_owner) XDestroyWindow(dpy, g_owner);
  XSync(dpy, False);
}

void handle(XEvent &e) {
  switch (e.type) {
  case CreateNotify:
    if (e.xcreatewindow.parent == root) add(e.xcreatewindow.window);
    break;
  case DestroyNotify:
    remove(e.xdestroywindow.window, true);
    break;
  case MapNotify:
    if (Win *w = find(e.xmap.window)) { w->mapped = true; track_damage(*w); g_dirty = true; }
    break;
  case UnmapNotify:
    if (Win *w = find(e.xunmap.window)) {
      w->mapped = false;
      free_win(*w);   // the picture of an unmapped window is useless; a new one is made when it maps
      g_dirty = true;
    }
    break;
  case ReparentNotify:
    if (e.xreparent.parent == root) add(e.xreparent.window);
    else remove(e.xreparent.window, false);
    break;
  case ConfigureNotify: {
    XConfigureEvent &c = e.xconfigure;
    if (c.window == root) {
      if (c.width != root_w || c.height != root_h) {   // the screen size changed (xrandr)
        root_w = c.width; root_h = c.height;
        XResizeWindow(dpy, g_glwin, root_w, root_h);
        create_back();
        create_texture();
        g_dirty = true;
      }
      break;
    }
    if (Win *w = find(c.window)) {
      bool resized = w->w != c.width || w->h != c.height;
      w->x = c.x; w->y = c.y; w->w = c.width; w->h = c.height; w->bw = c.border_width;
      if (resized && w->pict) { XRenderFreePicture(dpy, w->pict); w->pict = None; }
      restack(c.window, c.above);
      g_dirty = true;
    }
    break;
  }
  case CirculateNotify: {
    auto it = std::find_if(wins.begin(), wins.end(), [&](const Win &w) { return w.id == e.xcirculate.window; });
    if (it != wins.end()) {
      Win w = *it;
      wins.erase(it);
      if (e.xcirculate.place == PlaceOnTop) wins.push_back(w); else wins.insert(wins.begin(), w);
      g_dirty = true;
    }
    break;
  }
  case PropertyNotify:
    if (e.xproperty.window == root && e.xproperty.atom == A_SHADER) {
      std::string s = get_shader_property();
      if (s == "none" || s.empty()) g_quit = 1;
      else if (s != g_current) use_shader(s);
    }
    break;
  case SelectionClear:
    if (e.xselectionclear.selection == A_CM) g_quit = 1;   // another compositor took over
    break;
  default:
    if (e.type == damage_event + XDamageNotify) {
      auto &d = *(XDamageNotifyEvent *)&e;
      XDamageSubtract(dpy, d.damage, None, None);
      g_dirty = true;
    } else if (shape_event >= 0 && e.type == shape_event + ShapeNotify) {
      g_dirty = true;
    }
  }
}

void list_shaders() {
  std::map<std::string, std::string> found;
  for (auto &d : shader_dirs())
    if (DIR *dir = opendir(d.c_str())) {
      while (dirent *e = readdir(dir)) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".frag") == 0) {
          std::string name = n.substr(0, n.size() - 5);
          if (!found.count(name)) found[name] = d + "/" + n;
        }
      }
      closedir(dir);
    }
  for (auto &[n, p] : found) printf("%s\t%s\n", n.c_str(), p.c_str());
}

}  // namespace

int main(int argc, char **argv) {
  std::string arg = argc > 1 ? argv[1] : "";
  if (arg == "--list") { list_shaders(); return 0; }
  if (arg == "-h" || arg == "--help") {
    printf("usage: nirvana-shader [NAME|FILE] | --session | --list | --stop\n");
    return 0;
  }
  dpy = XOpenDisplay(nullptr);
  if (!dpy) { log("cannot open the display"); return 1; }
  scr = DefaultScreen(dpy);
  root = RootWindow(dpy, scr);
  root_w = DisplayWidth(dpy, scr);
  root_h = DisplayHeight(dpy, scr);
  A_SHADER = XInternAtom(dpy, "_NIRVANA_SHADER", False);
  if (arg == "--stop") { set_shader_property("none"); return 0; }

  std::string shader = arg.empty() || arg == "--session" ? configured_shader() : arg;
  if (shader.empty() || shader == "none") return 0;   // no effect chosen

  XSetErrorHandler(on_xerror);
  signal(SIGTERM, [](int) { g_quit = 1; });
  signal(SIGINT, [](int) { g_quit = 1; });
  signal(SIGHUP, [](int) { g_quit = 1; });
  if (!setup(shader)) { XCloseDisplay(dpy); return arg == "--session" ? 0 : 1; }

  const double frame = 1.0 / 60, anim_frame = 1.0 / 30;
  double last = 0;
  int fd = ConnectionNumber(dpy);
  while (!g_quit) {
    while (XPending(dpy) && !g_quit) {
      XEvent e;
      XNextEvent(dpy, &e);
      handle(e);
    }
    if (g_quit) break;
    double t = now();
    double wait_s = -1;
    if (g_dirty || g_animated) {
      double gap = g_animated && !g_dirty ? anim_frame : frame;
      if (t - last >= gap) {
        render();
        last = now();
        continue;
      }
      wait_s = gap - (t - last);
    }
    pollfd p = {fd, POLLIN, 0};
    poll(&p, 1, wait_s < 0 ? 1000 : std::max(1, (int)(wait_s * 1000)));
  }
  cleanup();
  XCloseDisplay(dpy);
  return 0;
}
