// Switch presentation and the three extra options rows (Resolution, Scaling, FPS) that the
// translated Game menu shows through Port::label / Port::change.
#pragma once

#include "gfx.h"

namespace gow {
namespace sw {

bool initDisplay();
void loadDisplaySettings();
void presentDisplay(const gow::Surface& s);
void shutdownDisplay();

}  // namespace sw
}  // namespace gow
