// Switch software audio backend (see platform_switch.cpp).
#pragma once

namespace gow {

// Starts the audout stream and the mixer thread. Returns false if the audio device is
// unavailable; the game then runs silently.
bool audioInit();

// Stops the mixer thread and releases audout.
void audioShutdown();

}  // namespace gow
