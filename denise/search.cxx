// Nirvana Desktop - Rambler: Find (the explorer's "Search" button, Window > Find…)
//
// Searches a folder (and its subfolders) in a thread: by the name (wildcards * ? or a part
// of it), by the kind recognized from the content (pictures, sounds, films, archives,
// documents, texts, programs, folders), by text inside the file, by metadata (artist,
// title, camera...), by size and by the date of the last change. Results come in while
// it searches; a double click opens, "Show in Folder" goes there in the explorer, "Copy to
// the Other Pane" copies (in the background, like every copy).
#include "desktop.h"
#include "i18n.h"
#include "metadata.h"
#include "sniff.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Multi_Browser.H>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
using namespace plat;

std::string ex_other_path(NWin *w);

namespace {

const char *CATS[] = {"", "image", "audio", "video", "archive", "document", "text", "program", "folder"};

struct Query {
  std::string root, name, text, metatext;
  int cat = 0;
  bool sub = true;
  double min_mb = -1, max_mb = -1;
  int days = 0;   // changed within the last N days (0: any time)
};

struct Hit { std::string path, kind, detail; uintmax_t size = 0; bool dir = false; };

struct Search {
  Query q;
  std::atomic<bool> stop{false}, done{false};
  std::atomic<long> scanned{0};
  std::mutex m;
  std::vector<Hit> hits;
};

std::string lower(std::string s) {
  for (auto &c : s) c = (char)tolower((unsigned char)c);
  return s;
}

bool contains_text(const std::string &path, const std::string &needle) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::string n = lower(needle), carry;
  char buf[1 << 16];
  size_t total = 0;
  while (in.read(buf, sizeof buf) || in.gcount()) {
    std::string chunk = carry + lower(std::string(buf, (size_t)in.gcount()));
    if (chunk.find(n) != std::string::npos) return true;
    carry = chunk.size() > n.size() ? chunk.substr(chunk.size() - n.size()) : chunk;
    total += (size_t)in.gcount();
    if (total > 64u * 1024 * 1024) break;   // very large files: the first 64 MB
  }
  return false;
}

void run(std::shared_ptr<Search> s) {
  const Query &q = s->q;
  std::string pat = lower(q.name);
  bool wild = pat.find_first_of("*?[") != std::string::npos;
  time_t since = q.days ? time(nullptr) - (time_t)q.days * 86400 : 0;
  std::string want_cat = CATS[q.cat];
  auto check = [&](const fs::directory_entry &de) {
    std::error_code ec;
    std::string path = de.path().string(), name = de.path().filename().string();
    bool dir = de.is_directory(ec);
    if (want_cat == "folder" && !dir) return;
    if (dir && want_cat != "folder" && (!want_cat.empty() || !q.text.empty() || !q.metatext.empty() || q.min_mb >= 0 || q.max_mb >= 0)) return;
    if (!pat.empty()) {
      std::string ln = lower(name);
      if (wild ? fnmatch(pat.c_str(), ln.c_str(), 0) != 0 : ln.find(pat) == std::string::npos) return;
    }
    uintmax_t size = dir ? 0 : de.file_size(ec);
    if (q.min_mb >= 0 && size < q.min_mb * 1048576) return;
    if (q.max_mb >= 0 && size > q.max_mb * 1048576) return;
    if (since) {
      auto t = de.last_write_time(ec);
      auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
      if (std::chrono::system_clock::to_time_t(sys) < since) return;
    }
    sniff::Kind k;
    if (!dir) k = sniff::of(path);   // the kind from the content, not the name
    if (!want_cat.empty() && want_cat != "folder" && k.cat != want_cat) return;
    std::string detail;
    if (!q.text.empty()) {
      if (dir || !contains_text(path, q.text)) return;
    }
    if (!q.metatext.empty()) {
      meta::Fields f = meta::read(path, k);
      std::string mt = lower(meta::text_of(f)), needle = lower(q.metatext);
      if (mt.find(needle) == std::string::npos) return;
      for (auto &[label, v] : f)
        if (lower(v).find(needle) != std::string::npos) { detail = label + ": " + v; break; }
    }
    Hit h{path, dir ? std::string(_("folder")) : k.label, detail, size, dir};
    std::lock_guard<std::mutex> l(s->m);
    s->hits.push_back(h);
  };
  std::error_code ec;
  if (q.sub) {
    for (auto it = fs::recursive_directory_iterator(q.root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (s->stop) break;
      std::string n = it->path().filename().string();
      if (!n.empty() && n[0] == '.' && pat.rfind(".", 0) != 0) {   // hidden: not unless asked for
        if (it->is_directory(ec)) it.disable_recursion_pending();
        continue;
      }
      if (it->is_symlink(ec) && it->is_directory(ec)) it.disable_recursion_pending();   // no loops
      s->scanned++;
      check(*it);
    }
  } else {
    for (auto it = fs::directory_iterator(q.root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
      if (s->stop) break;
      s->scanned++;
      check(*it);
    }
  }
  s->done = true;
}

std::string human(uintmax_t b) {
  char t[32];
  if (b < 1024) snprintf(t, sizeof t, "%ju B", b);
  else if (b < 1048576) snprintf(t, sizeof t, "%.0f KB", b / 1024.0);
  else snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  return t;
}

// "~/Muzyka" for people in the Look in field, the full path for the search
static std::string tilde(const std::string &p) {
  const char *home = getenv("HOME");
  size_t n = home ? strlen(home) : 0;
  if (n && p.compare(0, n, home) == 0 && (p.size() == n || p[n] == '/')) return "~" + p.substr(n);
  return p;
}
static std::string untilde(const std::string &p) {
  const char *home = getenv("HOME");
  if (home && !p.empty() && p[0] == '~' && (p.size() == 1 || p[1] == '/')) return home + p.substr(1);
  return p;
}

class FindWin : public Fl_Double_Window {
public:
  Desktop *desk;
  Fl_Input *root, *name, *text, *metatext, *min_mb, *max_mb;
  Fl_Choice *cat, *when;
  Fl_Check_Button *sub;
  Fl_Button *go, *reveal_b, *copy;
  Fl_Multi_Browser *list;
  Fl_Box *status;
  std::shared_ptr<Search> cur;
  size_t shown = 0;
  std::string status_s;

  FindWin(Desktop *d, const std::string &where) : Fl_Double_Window(720, 560, _("Find")), desk(d) {
    color(GRAY);
    xclass("nirvana-find");
    auto label = [&](int X, int Y, int W, const char *t) {
      auto *b = new Fl_Box(X, Y, W, 24, t);
      b->labelsize(SIZE_SMALL + 1);
      b->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    };
    auto input = [&](int X, int Y, int W) {
      auto *i = new Fl_Input(X, Y, W, 24);
      i->box(WELL_BOX);
      i->textsize(SIZE_SMALL + 1);
      return i;
    };
    label(10, 12, 130, _("Look in:"));
    root = input(144, 12, 440);
    root->value(tilde(where).c_str());
    sub = new Fl_Check_Button(592, 12, 120, 24, _("Subfolders"));
    sub->value(1);
    sub->labelsize(SIZE_SMALL + 1);
    label(10, 42, 130, _("Name:"));
    name = input(144, 42, 250);
    name->tooltip(_("A part of the name, or a pattern with * and ?"));
    label(400, 42, 70, _("Kind:"));
    cat = new Fl_Choice(474, 42, 230, 24);
    cat->textsize(SIZE_SMALL + 1);
    cat->add(_("Anything"));
    for (int i = 1; i < 8; i++) cat->add(sniff::category_label(CATS[i]).c_str());
    cat->add(_("Folders"));
    cat->value(0);
    label(10, 72, 130, _("Containing text:"));
    text = input(144, 72, 250);
    label(400, 72, 70, _("Tags:"));
    metatext = input(474, 72, 230);
    metatext->tooltip(_("Artist, title, album, camera... from inside the files"));
    label(10, 102, 130, _("Size (MB):"));
    min_mb = input(144, 102, 70);
    min_mb->tooltip(_("at least"));
    auto *dash = new Fl_Box(214, 102, 20, 24, "–");
    dash->labelsize(SIZE_SMALL + 1);
    max_mb = input(234, 102, 70);
    max_mb->tooltip(_("at most"));
    label(400, 102, 70, _("Changed:"));
    when = new Fl_Choice(474, 102, 230, 24);
    when->textsize(SIZE_SMALL + 1);
    for (const char *t : {_("any time"), _("today"), _("this week"), _("this month"), _("this year")}) when->add(t);
    when->value(0);
    go = new Fl_Button(604, 136, 100, 26, _("Find"));
    go->labelfont(FONT_SYSTEM);
    go->callback([](Fl_Widget *w, void *) { ((FindWin *)w->window())->toggle(); });
    go->shortcut(FL_Enter);
    list = new Fl_Multi_Browser(10, 170, 700, 330);
    list->box(WELL_BOX);
    list->textsize(SIZE_SMALL + 1);
    static int cols[] = {170, 150, 110, 64, 0};
    list->column_widths(cols);
    list->column_char('\t');
    list->callback([](Fl_Widget *w, void *) {
      auto *f = (FindWin *)w->window();
      if (Fl::event_clicks() && f->list->value() > 0) { Fl::event_clicks(0); f->open(f->list->value()); }
    });
    status = new Fl_Box(10, 504, 450, 22);
    status->labelsize(SIZE_SMALL);
    status->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    reveal_b = new Fl_Button(10, 528, 160, 24, _("Show in Folder"));
    reveal_b->callback([](Fl_Widget *w, void *) { ((FindWin *)w->window())->reveal(); });
    copy = new Fl_Button(176, 528, 200, 24, _("Copy to the Other Pane"));
    copy->callback([](Fl_Widget *w, void *) { ((FindWin *)w->window())->copy_other(); });
    for (Fl_Button *b : {go, reveal_b, copy}) { b->box(BUTTON_BOX); b->down_box(BUTTON_DOWN); if (b != go) b->labelsize(SIZE_SMALL + 1); }
    end();
    resizable(list);
    size_range(600, 400);
  }
  void toggle() {
    if (cur && !cur->done) { cur->stop = true; return; }
    auto s = std::make_shared<Search>();
    Query &q = s->q;
    q.root = untilde(root->value());
    if (q.root.rfind("~", 0) == 0) { const char *h = getenv("HOME"); q.root = std::string(h ? h : "") + q.root.substr(1); }
    q.name = name->value();
    q.text = text->value();
    q.metatext = metatext->value();
    q.cat = cat->value();
    q.sub = sub->value();
    q.min_mb = *min_mb->value() ? atof(min_mb->value()) : -1;
    q.max_mb = *max_mb->value() ? atof(max_mb->value()) : -1;
    static const int D[] = {0, 1, 7, 31, 365};
    q.days = D[std::clamp(when->value(), 0, 4)];
    cur = s;
    shown = 0;
    list->clear();
    go->label(_("Stop"));
    std::thread(run, s).detach();
    Fl::add_timeout(0.2, tick, this);
  }
  static void tick(void *d) {
    auto *f = (FindWin *)d;
    if (!f->cur) return;
    std::vector<Hit> add;
    {
      std::lock_guard<std::mutex> l(f->cur->m);
      add.assign(f->cur->hits.begin() + (long)f->shown, f->cur->hits.end());
    }
    for (auto &h : add) {
      fs::path p(h.path);
      std::string dir = tilde(p.parent_path().string());   // the folder for people: ~/Muzyka
      std::string row = (h.dir ? "@b" : "") + clean(p.filename().string()) + "\t" + clean(dir) + "\t" + clean(h.kind) + "\t" +
                        (h.dir ? "" : human(h.size)) + "\t" + clean(h.detail);
      f->list->add(row.c_str());
    }
    f->shown += add.size();
    bool done = f->cur->done;
    f->status_s = fmt(done ? _("Done: %1 checked, %2 found.") : _("Searching… %1 checked, %2 found."),
                      std::to_string(f->cur->scanned.load()), std::to_string(f->shown));
    f->status->label(f->status_s.c_str());
    f->status->redraw();
    if (done) { f->go->label(_("Find")); return; }
    Fl::repeat_timeout(0.2, tick, d);
  }
  static std::string clean(const std::string &s) {   // browser format characters
    std::string o;
    for (char c : s) o += c == '\t' ? ' ' : c;
    if (!o.empty() && o[0] == '@') o = "@." + o;
    return o;
  }
  const Hit *hit(int line) {
    if (!cur || line < 1) return nullptr;
    std::lock_guard<std::mutex> l(cur->m);
    return line <= (int)cur->hits.size() ? &cur->hits[line - 1] : nullptr;
  }
  std::vector<Entry> selected() {
    std::vector<Entry> v;
    for (int i = 1; i <= list->size(); i++)
      if (list->selected(i))
        if (const Hit *h = hit(i)) {
          Entry e;
          e.path = h->path;
          e.name = e.label = fs::path(h->path).filename().string();
          e.dir = h->dir;
          e.size = h->size;
          v.push_back(e);
        }
    return v;
  }
  void open(int line) {
    auto v = selected();
    if (const Hit *h = hit(line)) desk->open_found(h->path, false);
  }
  void reveal() {
    auto v = selected();
    if (!v.empty()) desk->open_found(v[0].path, true);
  }
  void copy_other() {
    auto v = selected();
    if (!v.empty()) desk->copy_found(v);
  }
};
FindWin *g_find;

}  // namespace

void Desktop::open_search() {
  NWin *a = active_folder();
  std::string where = a ? a->path : home_;
  if (!g_find) g_find = new FindWin(this, where);
  else g_find->root->value(tilde(where).c_str());
  g_find->show();
  g_find->name->take_focus();
}

// a found item: open it (a file with its type's double click), or show it in its folder
void Desktop::open_found(const std::string &path, bool in_folder) {
  std::error_code ec;
  NWin *w = nullptr;
  for (auto *o : wins_) if (o->pane && o->shown()) w = o;
  if (in_folder || fs::is_directory(path, ec)) {
    std::string dir = in_folder ? fs::path(path).parent_path().string() : path;
    if (w) { navigate(w, dir); w->activate_window(); } else open_folder(dir);
    for (auto *o : wins_) if (o->pane && o->path == dir && in_folder) o->pane->view()->select_path(path);
    return;
  }
  Entry e;
  e.path = path;
  e.name = e.label = fs::path(path).filename().string();
  open_entry(e, nullptr);
}

void Desktop::copy_found(const std::vector<Entry> &items) {
  NWin *w = nullptr;
  for (auto *o : wins_) if (o->pane && o->ex) w = o;
  std::string dest = w ? ex_other_path(w) : "";
  if (dest.empty() && w) dest = w->path;
  if (dest.empty()) { dialog(_("Find"), _("Open a folder window to copy into."), {{"OK", nullptr}}); return; }
  transfer(items, dest, true, false);
}
