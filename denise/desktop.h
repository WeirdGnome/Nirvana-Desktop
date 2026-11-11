// Nirvana Desktop - denise: biurko, pasek menu i okna menedżera plików (Rambler)
//
// Biurko (wzór, ikony dysku i kosza, globalny pasek menu) to jedno okno X
// typu DESKTOP. Każdy folder i każdy dialog to osobne okno X - ramki,
// przesuwanie, zwijanie i fokus zapewnia menedżer okien (Insight).
// Pasek menu działa na aktywne okno, które odczytujemy z _NET_ACTIVE_WINDOW.
#pragma once

#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Tiled_Image.H>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "anim.h"
#include "folder_view.h"
#include "gary.h"
#include "net.h"
#include "volumes.h"
#include "spatial.h"

class Desktop;
void rambler_spawn(const std::vector<std::string> &args);   // podwójny fork
std::string rambler_exe(const char *name);                  // np. "nirvana-shell"
std::string tooltypes(const std::string &path);             // parametry uruchamiania (Informacje)
std::vector<std::string> split_args(const std::string &s);

// Okno najwyższego poziomu Ramblera: folder albo dialog.
class NWin : public Fl_Double_Window {
public:
  NWin(Desktop *d, int X, int Y, int W, int H, const char *title, bool dialog);

  Desktop *desk;
  FolderPane *pane = nullptr;                  // nullptr dla dialogów
  std::string path;
  std::vector<std::function<void()>> actions;  // akcje przycisków dialogu
  bool dialog;
  // Własna ramka Platinum (CSD), gdy menedżerem okien nie jest Insight -
  // np. pod GNOME. Wtedy okno jest bez dekoracji, a treść leży w środku ramki.
  bool csd;
  int ox() const { return csd ? plat::FRAME : 0; }          // przesunięcie treści
  int oy() const { return csd ? plat::TITLE_H : 0; }
  int extra_w() const { return csd ? 2 * plat::FRAME : 0; } // ramka razem
  int extra_h() const { return csd ? plat::TITLE_H + plat::FRAME : 0; }
  plat::Rect content_rect();  // położenie treści na ekranie (pełna wysokość)

  bool is_active();     // czy to okno ma fokus (wg menedżera okien)
  bool shaded();       // czy menedżer okien je zwinął
  void set_shaded(bool s);    // poproś menedżer okien o zwinięcie/rozwinięcie
  void activate_window();     // wyciągnij na wierzch i daj fokus
  plat::Rect frame_rect();   // przybliżony obrys z ramką (do animacji)

  ~NWin() override;
  int handle(int e) override;   // skróty klawiszowe trafiają do globalnego paska menu
  void resize(int X, int Y, int W, int H) override;
  void draw() override;

private:
  // stan własnej ramki
  int csd_handle(int e);
  int hit_button(int ex, int ey) const;
  void csd_toggle_shade();
  void csd_toggle_zoom();
  void start_moveresize(int direction);
  bool csd_shaded_ = false, csd_zoomed_ = false;
  int full_h_ = 0, unzoom_[4] = {0, 0, 0, 0};
  enum { NONE, PENDING, MOVE, GROW, BTN } drag_ = NONE;   // PENDING: wciśnięte, jeszcze bez ruchu
  int press_x_ = 0, press_y_ = 0;
  int drag_dx_ = 0, drag_dy_ = 0, armed_ = 0, pressed_ = 0;
};

class Desktop : public Fl_Double_Window {
public:
  Desktop(int W, int H, const std::string &home, bool root);

  void open_folder(const std::string &path, bool animate = true);
  void restore();                 // otwórz okna zapamiętane z poprzedniej sesji
  void close(NWin *w);            // zamknięcie okna (przycisk ramki, Ctrl+W, dialog)
  void remember(NWin *w, bool open);
  void active_changed();          // menedżer okien zmienił aktywne okno
  void clients_changed();         // zmieniła się lista okien (_NET_CLIENT_LIST)
  void current_app(std::string &name, Fl_Image *&icon);   // aktywna aplikacja dla paska
  std::vector<NWin *> folder_windows();                    // widoczne okna folderów
  int shortcut();                 // skrót z innego okna -> pasek menu

  // Środowisko okien (wykrywane przy starcie)
  bool csd_mode = false;          // menedżer okien to nie Insight: rysujemy ramki sami
  bool wm_present = false;        // jakikolwiek menedżer okien zgodny z EWMH
  bool wm_moveresize = false;     // WM obsługuje _NET_WM_MOVERESIZE (przesuwanie za nas)
  NWin *manual_active = nullptr;  // aktywne okno, gdy nie ma menedżera okien
  void redraw_frames();           // odśwież ramki (zmiana aktywnego okna)
  void detect_wm();
  plat::Rect desk_rect() { return {x_root(), y_root(), w(), h()}; }

  int handle(int e) override;
  void draw() override;

  // akcje menu
  void about();
  void new_folder();
  void open_selected();
  void close_active();
  void move_to_trash();
  void empty_trash();
  void ask_root();
  void refresh_all();
  void appearance_changed();      // panel Wygląd zmienił schemat (_NIRVANA_WYGLAD)
  void open_appearance();         // uruchom panel Wygląd (nirvana-alice)
  void open_network();            // panel Sieć
  void open_power();              // panel Zasilanie
  void open_terminal();           // Nirvana Shell w folderze aktywnego okna
  void open_terminal_home();
  // sieć (pasek menu)
  void build_net_menu(Fl_Menu_Button *m);
  void net_action(bool connect, const std::string &iface);
  void net_refresh();
  void vpn_toggle(const net::Vpn &v, const std::string &msg);
  void wifi_connect(const net::Network &n, const std::string &msg);
  const net::State &net_state() const { return net_; }
  void dialog_message(const std::string &title, const std::string &text);
  void quit();
  void build_windows_menu(Fl_Menu_Button *m);
  void rename_selected();
  void eject_selected();
  void duplicate();
  void copy_selected();
  void paste();
  // pliki przeciągnięte z innych programów (lista file://) - kopiujemy do dest
  void drop_uris(const std::string &uri_list, const std::string &dest);
  void select_all();
  void view_icons();
  void view_list();
  void shade_all(bool shade);
  void close_all();
  // --- menu Nirvana / Okno / Ikony / Narzędzia (menus.cxx) ---
  void update_menu();            // aktywne/nieaktywne pozycje przed otwarciem menu
  void open_preferences();
  void execute_command();
  void open_log();
  // menu System (applets::power_action)
  void system_lock();
  void system_sleep();
  void system_hibernate();
  void system_logout();
  void system_restart();
  void system_shutdown();
  void open_parent();
  void update_window();
  void clean_up();
  void toggle_hidden();
  void sort_name();
  void sort_date();
  void sort_size();
  void sort_kind();
  void icons_small();
  void icons_normal();
  void icons_large();
  void get_info();
  void set_as_wallpaper();   // zaznaczony obraz na biurko
  void snapshot();
  void unsnapshot();
  void dock_add();
  void dock_remove();
  void format_disk();
  void reset_nirvana();
  static void set_argv(int argc, char **argv);
  NWin *begin_form(const std::string &title, int W, int H, const Entry *icon, const std::string &text,
                   int &ox, int &oy);
  void end_form(NWin *win, int W, int H, int ox, int oy,
                std::vector<std::pair<std::string, std::function<void()>>> buttons);

private:
  struct DeskIcon {
    std::string name, path;
    plat::Icon icon;
    int x, y;
    int vol = -1;   // indeks w vols_ dla ikon dysków
    bool dock = false;   // ikona wyciągnięta na biurko (dok w lewym dolnym rogu)
    Entry entry;         // dla doku: plik/folder/aplikacja
    unsigned long win = 0;      // ikona zikonizowanego okna (Window); dwuklik je przywraca
    Fl_Image *img = nullptr;    // jej obrazek (z _NET_WM_ICON albo AppImage)
  };
  int icon_cols_ = 1;   // kolumny ikon przy lewej krawędzi (dyski, okna, kosz)
  // dok: ścieżki w ~/.config/nirvana/dok
  std::vector<std::string> dock_;
  bool dock_collapsed_ = false;   // dok zwinięty do samej zakładki
  int dock_shown_ = 0;            // ile ikon doku się zmieściło
  int dock_press_ = -1;           // wciśnięty kafelek doku (otwarcie po puszczeniu)
  int tip_icon_ = -1;             // kafelek z widoczną podpowiedzią
  void dock_rect(int &X, int &Y, int &W, int &H) const;
  void draw_dock();
  void toggle_dock();
  void save_rambler_conf();
  void load_dock();
  void save_dock();
  void format_dialog(int vi);
  std::vector<std::string> targets();   // zaznaczone ikony (okno albo biurko)
  void set_icon_size(int s);
  void set_sort(FolderView::SortKey k);
  bool show_hidden_ = false;
  // --- dyski i płyty ---
  std::vector<vol::Volume> vols_;
  void watch_volumes();
  void rescan_volumes();
  static void rescan_later(void *d);
  static void poll_volumes(void *d);
  void open_volume(int vi);
  void eject_volume(int vi);
  void ask_password(const vol::Volume &v, const std::string &msg);
  void ask_secret(const std::string &title, plat::Icon icon, const std::string &text,
                  const std::string &msg, const char *ok_label,
                  std::function<void(std::string &)> on_ok);
  int volume_at_path(const std::string &path) const;   // wolumen zawierający ścieżkę
  // przeciąganie ikony dysku po biurku (do kosza = wysuń)
  int vdrag_icon_ = -1, vpress_x_ = 0, vpress_y_ = 0;
  bool vdragging_ = false;
  Fl_Window *vghost_ = nullptr;

  NWin *find(const std::string &path);
  bool known(NWin *w) const;
  NWin *active_folder();          // aktywne okno folderu (lub nullptr)
  void layout_icons();
public:
  void iconic_changed() { icon_sel_ = -1; layout_icons(); redraw(); }
  // Obszar na okna folderów (treść okna, współrzędne biurka): bez kolumny ikon
  // dysków i kosza z prawej i bez doku na dole - zawsze je widać, też po zoomie.
  plat::Rect folder_area() const;
private:
  void publish_folder_area();
  Fl_RGB_Image *render_wallpaper(int W, int H, bool *ok);
  void watch_trash();
  int icon_at(int ex, int ey) const;
  bool allowed(const std::string &path) const;
  bool source_rect(const std::string &path, plat::Rect &out);  // ikona, z której "wylatuje" okno
  plat::Rect to_screen(plat::Rect r) const { return {r.x + x_root(), r.y + y_root(), r.w, r.h}; }
  std::string title_for(const std::string &path) const;
  std::string trash_dir() const;
  bool trash_full() const;
  void open_entry(const Entry &e);
  void open_appimage(const std::string &path);
  void run_app(const std::string &path);
  std::string apps_dir() const;
  bool in_apps(const std::string &path) const;
  std::string app_destination(const std::string &path) const;
  // operacje na plikach
  std::string rename_path(const std::string &old, const std::string &name);
  bool trash_path(const std::string &path, std::string &err);
  void transfer(const std::vector<Entry> &items, const std::string &dest, bool copy, bool replace);
  void path_moved(const std::string &from, const std::string &to);   // okna i pamięć pozycji
  void sync_view_menu();
  // przeciąganie
  struct DropTarget { NWin *win = nullptr; int item = -2; int icon = -1; };
  DropTarget target_at(int xr, int yr);
  void drag_start(NWin *src, const std::vector<Entry> &items);
  void drag_move(int xr, int yr);
  void drag_end(int xr, int yr, bool copy);
  void clear_drop_highlight();
  struct {
    bool active = false;
    NWin *src = nullptr;
    std::vector<Entry> items;
    Fl_Window *ghost = nullptr;
    NWin *hl_win = nullptr;
    int px = 0, py = 0;   // gdzie zaczęło się przeciąganie (ekran)
  } drag_;
  int icon_drop_ = -1;            // ikona biurka podświetlona jako cel upuszczania
  std::vector<std::string> clipboard_;   // ścieżki skopiowane przez Edycja > Kopiuj
  void dialog(const std::string &title, const std::string &text,
              std::vector<std::pair<std::string, std::function<void()>>> buttons);
  static void dialog_cb(Fl_Widget *b, void *idx);

  std::string home_, root_path_;
  bool root_;
  Spatial spatial_;
  std::vector<NWin *> wins_;      // kolejność: ostatnio aktywne na końcu
  std::vector<DeskIcon> icons_;
  int icon_sel_ = -1;
  Fl_Menu_Bar *menu_;
  Fl_Widget *app_menu_ = nullptr;   // gary: menu aplikacji w prawym górnym rogu
  int active_app_index(const std::vector<gary::App> &apps);
  Fl_Widget *extras_;
  Fl_Widget *vol_w_ = nullptr, *pow_w_ = nullptr, *tray_w_ = nullptr, *mem_w_ = nullptr;
  bool tray_pending_ = false;
public:
  void layout_bar();        // rozmieszczenie apletów i zasobnika na pasku menu
  void start_tray();        // po show(): zasobnik systemowy (XEmbed + StatusNotifierItem)
private:
  std::unique_ptr<Fl_Tiled_Image> pattern_;
  std::unique_ptr<Fl_RGB_Image> wallpaper_;   // biurko z tapetą (wzór + obraz), gdy jest tapeta
  Fl_Menu_Item *logo_item_ = nullptr;
  void set_logo();
  Fl_RGB_Image *logo_img_ = nullptr;
  net::State net_;
  Fl_Widget *net_menu_ = nullptr;
};
