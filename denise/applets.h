// Nirvana Desktop - aplety paska menu: głośność i zasilanie (z trybem projekcji)
//
// Ikony siedzą po prawej stronie paska menu Ramblera. Kliknięcie otwiera
// okienko w stylu Platinum z suwakami i przyciskami; kółko myszy nad ikoną
// głośności zmienia głośność. Klawisze multimedialne (głośność, wyciszenie,
// jasność) łapiemy na oknie głównym X i pokazujemy OSD.
//
// W sesji Nirvana Desktop (pod Insightem) aplet zasilania prowadzi też bezczynność:
// wygaszanie i blokadę ekranu, usypianie oraz ostrzeżenia o baterii.
#pragma once

#include <FL/Fl_Widget.H>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace applets {

using Buttons = std::vector<std::pair<std::string, std::function<void()>>>;

struct Host {
  std::function<void(const std::string &title, const std::string &text, Buttons)> dialog;
  std::function<void()> logout;              // koniec sesji
  std::function<void()> before_shutdown;     // zapis stanu okien przed wyłączeniem
  std::function<void()> open_power_panel;    // panel Zasilanie (nirvana-alice)
  bool nirvana_session = true;                // pod Insightem: bezczynność prowadzi Nirvana Desktop
};

Fl_Widget *make_volume(int X, int Y, int W, int H);
Fl_Widget *make_power(int X, int Y, int W, int H);
void init(const Host &host);   // klawisze multimedialne, bezczynność, odświeżanie

}  // namespace applets
