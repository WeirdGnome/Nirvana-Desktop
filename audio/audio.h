// Nirvana Desktop - dźwięk: głośność wyjść, wejść i aplikacji
//
// Główna ścieżka: PulseAudio albo PipeWire (pipewire-pulse) przez `pactl`
// (tekstowe wyjście w LC_ALL=C - JSON w pactl 16 psuje polskie znaki).
// Zmiany na bieżąco z `pactl subscribe`. Bez serwera dźwięku: czysta ALSA
// przez `amixer` (Master i Capture), odpytywana co 2 s.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace audio {

struct Port { std::string name, desc; bool available = true; };

struct Device {                  // wyjście (sink) albo wejście (source)
  int index = -1;
  std::string name, desc;
  int volume = 0;                // procent, średnia kanałów
  bool mute = false;
  std::vector<Port> ports;
  std::string active_port;
};

struct Stream {                  // dźwięk aplikacji (sink input)
  int index = -1;
  std::string app, media, binary;
  int volume = 0;
  bool mute = false;
  int sink = -1;                 // indeks wyjścia
};

struct State {
  enum Backend { Brak, Pulse, Alsa } backend = Brak;
  std::string problem;
  std::vector<Device> sinks, sources;   // bez monitorów
  std::vector<Stream> streams;
  std::string def_sink, def_source;     // nazwy domyślnych
  const Device *sink() const;           // domyślne wyjście (albo nullptr)
  const Device *source() const;
};

State query();

// Operacje (synchroniczne - pactl/amixer odpowiadają w kilka ms).
void set_volume(const State &s, bool output, const std::string &dev, int percent);
void set_mute(const State &s, bool output, const std::string &dev, bool mute);
void set_default(bool output, const std::string &dev);   // + przeniesienie strumieni
void set_port(bool output, const std::string &dev, const std::string &port);
void set_stream_volume(int index, int percent);
void set_stream_mute(int index, bool mute);
void move_stream(int index, const std::string &sink);

// Zmiana głośności domyślnego wyjścia o delta (klawisze multimedialne, kółko).
void step_volume(int delta);
void toggle_mute(bool output);

void watch(std::function<void()> changed);

}  // namespace audio
