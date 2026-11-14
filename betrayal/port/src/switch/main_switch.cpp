// Nintendo Switch entry point: libnx applet/framebuffer/pad lifecycle around the translated game.
#include <switch.h>

#include <cstdio>
#include <string>

#include "audio_switch.h"
#include "display_switch.h"
#include "gen/classes.h"
#include "input.h"
#include "platform.h"
#include "switch/data_switch.h"
#include "switch/switch_util.h"

namespace {

gow::Surface g_screen;
gow::Game* g_game = nullptr;

void gameKeyDown(int code) {
  if (g_game) g_game->platformKey(code, true);
}
void gameKeyUp(int code) {
  if (g_game) g_game->platformKey(code, false);
}

void errorScreen(const std::string& message) {
  consoleInit(NULL);
  consoleClear();
  std::printf("\n  God of War: Betrayal (Switch port)\n\n");
  std::printf("  Reason:\n%s\n\n", message.c_str());
  std::printf("  Put the Sony Ericsson K800i .jar directly in:\n");
  std::printf("    sdmc:/switch/gow-betrayal\n");
  std::printf("  named game.jar, or as the only .jar in that folder.\n\n");
  std::printf("  Then start the app again. Press + to exit.\n");
  consoleUpdate(NULL);

  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  PadState pad;
  padInitializeDefault(&pad);
  const u64 start = armGetSystemTick();
  while (appletMainLoop()) {
    padUpdate(&pad);
    if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
    if ((armGetSystemTick() - start) / armGetSystemTickFreq() > 12) break;
    svcSleepThread(16000000ULL);
  }

  consoleExit(NULL);
}

}  // namespace

int main(int, char**) {
  // libnx's __appInit already initializes fs and mounts sdmc: before main(). Do not call
  // fsInitialize()/fsdevMountSdmc() again: a second mount fails even when the card is fine.
  std::string error;
  if (!gow::sw::prepareGameData(error)) {
    errorScreen(error);
    return 1;
  }

  gow::sw::loadDisplaySettings();
  if (!gow::sw::initDisplay()) {
    errorScreen("Could not create the Switch framebuffer.");
    return 1;
  }
  gow::audioInit();  // silent fallback if the audio device is unavailable

  gow::Platform::dataDir = gow::sw::dataPath();
  gow::Platform::saveDir = gow::sw::savesPath();

  gow::GOWMIDlet* midlet = new gow::GOWMIDlet();
  midlet->startApp();
  gow::Game* game = static_cast<gow::Game*>(midlet->game);
  if (!game || !game->ok()) {
    gow::audioShutdown();
    gow::sw::shutdownDisplay();
    errorScreen("Could not read the game data (RP1...RP33).");
    return 1;
  }
  if (!game->supportedBuild()) {
    gow::audioShutdown();
    gow::sw::shutdownDisplay();
    errorScreen(
        "These game files are from a different God of War: Betrayal build.\n"
        "The port needs the Sony Ericsson K800i version (MIDlet 1.4.20).");
    return 1;
  }

  g_game = game;
  input::init(gameKeyDown, gameKeyUp);

  bool focused = true;
  while (appletMainLoop()) {
    const bool nowFocused = appletGetFocusState() == AppletFocusState_InFocus;
    if (nowFocused != focused) {
      focused = nowFocused;
      if (focused) {
        game->shown = true;
        game->showNotify();
      } else {
        input::releaseAll();
        game->shown = false;
        game->hideNotify();
      }
    }
    input::poll(gow::Game::state != 100);
    const double frameStart = gow::sw::nowMs();
    const int sleepMs = game->runFrame(frameStart, g_screen);
    if (game->quit) break;
    // framebufferBegin waits for the display; subtract that wait from the engine's requested
    // sleep so 25 Hz stays 25 Hz and the 60 Hz option really reaches 60.
    gow::sw::presentDisplay(g_screen);
    const int elapsed = (int)(gow::sw::nowMs() - frameStart);
    const int remaining = sleepMs - elapsed;
    if (remaining > 0) svcSleepThread((s64)remaining * 1000000LL);
  }

  gow::audioShutdown();
  gow::sw::shutdownDisplay();
  return 0;  // libnx unmounts fs and exits its services after main returns
}
