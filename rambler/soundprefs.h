/*
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
    Port to FLTK: Nirvana Desktop contributors.

    Licensed under the AROS Public License, version 1.1 (see LICENSE in the repository root).

    The Sound preferences: what the beep (DisplayBeep) does - the screen flashes, a sound
    is made, or both; the sound a beep (its tone, length and volume) or a sampled sound (a
    file), with a Test button. Use writes the settings in use (ENV:), Save also
    ~/.config/rambler/sound.prefs (ENVARC:); the window manager takes them up at once (the
    _RAMBLER_SOUND property of the root window). With PulseAudio or PipeWire (pactl) also
    the sound output (AHI's part): the output device, its volume and mute.
*/
#pragma once

namespace rambler {

void edit_sound_prefs(bool standalone);

}  // namespace rambler
