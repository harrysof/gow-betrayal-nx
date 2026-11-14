// Switch controller -> the MIDP key codes Engine expects. The physical layout follows the original
// PS2-style mapping in a level, and the standard A=confirm / B=back convention in menus.
#include "input.h"

#include <switch.h>

#include <cstring>

namespace input {

namespace {

void (*g_down)(int) = nullptr;
void (*g_up)(int) = nullptr;

constexpr int kUp = -1, kDown = -2, kLeft = -3, kRight = -4, kFire = -5, kSoftL = -6, kSoftR = -7;
constexpr int kHash = 35;

PadState g_padState;
int g_pad[8];
const int kCodes[8] = {kUp, kDown, kLeft, kRight, kFire, kSoftL, kSoftR, kHash};

}  // namespace

void init(void (*keyDown)(int), void (*keyUp)(int)) {
  g_down = keyDown;
  g_up = keyUp;
  std::memset(g_pad, 0, sizeof g_pad);
  padConfigureInput(1, HidNpadStyleSet_NpadStandard);
  padInitializeDefault(&g_padState);
}

// The Switch backend is pad-only; this entry point exists for parity with the PC input layer.
void keyEvent(unsigned, bool, bool) {}

void releaseAll() {
  for (int i = 0; i < 8; i++)
    if (g_pad[i]) {
      g_pad[i] = 0;
      if (g_up) g_up(kCodes[i]);
    }
}

void poll(bool inMenu) {
  padUpdate(&g_padState);
  const u64 k = padGetButtons(&g_padState);
  const HidAnalogStickState stick = padGetStickPos(&g_padState, 0);
  const int dead = 12000;
  const bool stickUp = stick.y > dead;
  const bool stickDown = stick.y < -dead;
  const bool stickLeft = stick.x < -dead;
  const bool stickRight = stick.x > dead;

  bool pressed[8] = {};
  // In a level: South (B) jumps, East (A) grabs/blocks, West (Y) attacks, North (X) changes
  // weapon/magic. In menus A confirms and B backs out; the d-pad/stick alone moves the cursor.
  pressed[0] = (k & HidNpadButton_Up) || stickUp || (!inMenu && (k & HidNpadButton_B));
  pressed[1] = (k & HidNpadButton_Down) || stickDown || (!inMenu && (k & HidNpadButton_A));
  pressed[2] = (k & HidNpadButton_Left) || stickLeft;
  pressed[3] = (k & HidNpadButton_Right) || stickRight;
  pressed[4] = (k & HidNpadButton_Y) != 0;
  pressed[5] = (k & (HidNpadButton_X | HidNpadButton_R)) || (inMenu && (k & HidNpadButton_A));
  pressed[6] = (k & HidNpadButton_Plus) || (inMenu && (k & HidNpadButton_B));
  pressed[7] = (k & (HidNpadButton_Minus | HidNpadButton_L | HidNpadButton_ZL)) != 0;

  for (int i = 0; i < 8; i++) {
    if (pressed[i] && !g_pad[i]) {
      g_pad[i] = kCodes[i];
      if (g_down) g_down(kCodes[i]);
    } else if (!pressed[i] && g_pad[i]) {
      g_pad[i] = 0;
      if (g_up) g_up(kCodes[i]);
    }
  }
}

}  // namespace input
