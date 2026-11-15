/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The beep (DisplayBeep) as the Sound preferences set it up, shared by the window manager
    (which beeps) and the preferences program (whose Test button beeps). The settings, one
    a line ("name=value"), in sound.prefs (ENV: $XDG_RUNTIME_DIR/rambler, ENVARC:
    ~/.config/rambler) and in the _RAMBLER_SOUND property of the root window:
      flash      1: the screen flashes
      sound      1: a sound is made
      type       0 a beep (a tone), 1 a sampled sound (a file)
      sample     the sound file
      volume     0..100
      pitch      the beep's tone in Hz (110..3520)
      duration   the beep's length in hundredths of a second (1..200)
    The sound is played by paplay (PulseAudio/PipeWire), else aplay (ALSA), in a process of
    its own so that nothing waits for it.
*/
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cerrno>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace rambler {

struct BeepPrefs {
    bool flash = true, sound = false;
    int type = 0;                // 0 beep, 1 sampled sound
    std::string sample;
    int volume = 64, pitch = 880, duration = 15;

    std::string text() const {
        return "flash=" + std::to_string(flash) + "\nsound=" + std::to_string(sound) + "\ntype=" + std::to_string(type) +
               "\nsample=" + sample + "\nvolume=" + std::to_string(volume) + "\npitch=" + std::to_string(pitch) +
               "\nduration=" + std::to_string(duration) + "\n";
    }
    static BeepPrefs parse(const std::string &t) {
        BeepPrefs p;
        size_t a = 0;
        while (a < t.size()) {
            size_t b = t.find('\n', a);
            if (b == std::string::npos) b = t.size();
            std::string line = t.substr(a, b - a);
            a = b + 1;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), v = line.substr(eq + 1);
            int n = atoi(v.c_str());
            if (k == "flash") p.flash = n;
            else if (k == "sound") p.sound = n;
            else if (k == "type") p.type = std::clamp(n, 0, 1);
            else if (k == "sample") p.sample = v;
            else if (k == "volume") p.volume = std::clamp(n, 0, 100);
            else if (k == "pitch") p.pitch = std::clamp(n, 110, 3520);
            else if (k == "duration") p.duration = std::clamp(n, 1, 200);
        }
        return p;
    }
};

inline std::string beep_env_dir() {
    const char *rt = getenv("XDG_RUNTIME_DIR");
    return rt && *rt ? std::string(rt) + "/rambler" : "/tmp/rambler-" + std::to_string(getuid());
}

inline std::string beep_saved_file() {
    const char *x = getenv("XDG_CONFIG_HOME");
    std::string base = x && *x ? x : std::string(getenv("HOME") ? getenv("HOME") : "/tmp") + "/.config";
    return base + "/rambler/sound.prefs";
}

// the settings in use, else the saved ones
inline BeepPrefs load_beep_prefs() {
    for (const std::string &f : {beep_env_dir() + "/sound.prefs", beep_saved_file()}) {
        if (FILE *fp = fopen(f.c_str(), "r")) {
            std::string t;
            char buf[1024];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, fp)) > 0) t.append(buf, n);
            fclose(fp);
            return BeepPrefs::parse(t);
        }
    }
    return BeepPrefs();
}

// the beep as a sound file (16-bit mono WAV): a sine with short soft edges, so it does not
// click
inline bool write_beep_wav(const std::string &path, const BeepPrefs &p) {
    const int rate = 22050;
    int n = rate * p.duration / 100, edge = std::min(n / 4, rate / 200);
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) return false;
    auto u32 = [f](uint32_t v) { unsigned char b[4] = {(unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24)}; fwrite(b, 1, 4, f); };
    auto u16 = [f](unsigned v) { unsigned char b[2] = {(unsigned char)v, (unsigned char)(v >> 8)}; fwrite(b, 1, 2, f); };
    fwrite("RIFF", 1, 4, f); u32(36 + n * 2); fwrite("WAVEfmt ", 1, 8, f);
    u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    fwrite("data", 1, 4, f); u32(n * 2);
    double amp = 32000.0 * p.volume / 100.0;
    for (int i = 0; i < n; i++) {
        double env = 1.0;
        if (edge > 0 && i < edge) env = (double)i / edge;
        else if (edge > 0 && i > n - edge) env = (double)(n - i) / edge;
        int s = (int)lround(amp * env * sin(2 * M_PI * p.pitch * i / rate));
        u16((unsigned)(int16_t)s);
    }
    fclose(f);
    return true;
}

// the sound, in a process of its own (the caller does not wait; the child is not left behind
// as a zombie: it is the grandchild that plays)
inline void play_beep_sound(const BeepPrefs &p) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid > 0) { int st; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {} return; }
    if (fork() != 0) _exit(0);
    setsid();
    std::string file = p.sample;
    if (p.type == 0 || file.empty()) {
        std::string dir = beep_env_dir();
        mkdir(dir.c_str(), 0700);
        file = dir + "/beep.wav";
        if (!write_beep_wav(file, p)) _exit(1);
    }
    // the volume: paplay's own for a sample (the beep is made at its volume already)
    std::string vol = std::to_string(p.type == 0 ? 65536 : 65536 * p.volume / 100);
    execlp("paplay", "paplay", ("--volume=" + vol).c_str(), file.c_str(), (char *)nullptr);
    execlp("aplay", "aplay", "-q", file.c_str(), (char *)nullptr);
    _exit(127);
}

}  // namespace rambler
