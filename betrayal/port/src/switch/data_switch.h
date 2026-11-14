// First-run data setup for the Switch port: the user drops the K800i .jar on the SD card and
// this extracts its RP* resource banks next to the .nro. No game data is built into the .nro.
#pragma once

#include <string>

namespace gow {
namespace sw {

// Returns true when sdmc:/switch/gow-betrayal/data holds a supported K800i data set. If it does
// not, tries game.jar / the usual jar names in the app folder and extracts the RP* entries.
// On failure `error` is a user-facing sentence.
bool prepareGameData(std::string& error);

}  // namespace sw
}  // namespace gow
