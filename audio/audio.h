// Nirvana Desktop - sound: volume of outputs, inputs and applications
//
// Main path: PulseAudio or PipeWire (pipewire-pulse) through `pactl`
// (text output with LC_ALL=C - the JSON of pactl 16 breaks non-ASCII characters).
// Live changes from `pactl subscribe`. Without a sound server: plain ALSA
// through `amixer` (Master and Capture), polled every 2 s.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace audio {

struct Port { std::string name, desc; bool available = true; };

struct Device {                  // output (sink) or input (source)
  int index = -1;
  std::string name, desc;
  int volume = 0;                // percent, average of the channels
  bool mute = false;
  std::vector<Port> ports;
  std::string active_port;
};

struct Stream {                  // application sound (sink input)
  int index = -1;
  std::string app, media, binary;
  int volume = 0;
  bool mute = false;
  int sink = -1;                 // output index
};

struct State {
  enum Backend { NoBackend, Pulse, Alsa } backend = NoBackend;
  std::string problem;
  std::vector<Device> sinks, sources;   // without monitors
  std::vector<Stream> streams;
  std::string def_sink, def_source;     // names of the defaults
  const Device *sink() const;           // the default output (or nullptr)
  const Device *source() const;
};

State query();

// Operations (synchronous - pactl/amixer answer within a few ms).
void set_volume(const State &s, bool output, const std::string &dev, int percent);
void set_mute(const State &s, bool output, const std::string &dev, bool mute);
void set_default(bool output, const std::string &dev);   // + moving the streams
void set_port(bool output, const std::string &dev, const std::string &port);
void set_stream_volume(int index, int percent);
void set_stream_mute(int index, bool mute);
void move_stream(int index, const std::string &sink);

// Changing the default output's volume by delta (multimedia keys, wheel).
void step_volume(int delta);
void toggle_mute(bool output);

void watch(std::function<void()> changed);

}  // namespace audio
