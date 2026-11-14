#include "switch/display_switch.h"

#include <switch.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "platform.h"
#include "switch/switch_util.h"

namespace gow {
namespace sw {

namespace {

enum class Aspect { Original, Auto, R4_3, R16_10, R16_9, R21_9, Count };

struct Settings {
  Aspect aspect = Aspect::Auto;
  bool integerScaling = false;
  int fps = 60;  // 0 = original 25 steps/s, 60 = interpolated 60 fps
};

Settings g_settings;
Framebuffer g_fb;
bool g_displayReady = false;

std::string configPath() { return join(rootPath(), "display.cfg"); }

const char* aspectName(Aspect a) {
  switch (a) {
    case Aspect::Original: return "Original";
    case Aspect::Auto: return "Auto";
    case Aspect::R4_3: return "4:3";
    case Aspect::R16_10: return "16:10";
    case Aspect::R16_9: return "16:9";
    case Aspect::R21_9: return "21:9";
    default: return "";
  }
}

int logicalWidth(int clientW, int clientH) {
  const int H = gow::kScreenH, base = gow::kScreenW, maxW = H * 24 / 9;
  auto ratio = [&](int num, int den) { return (H * num + den / 2) / den; };
  int w = base;
  switch (g_settings.aspect) {
    case Aspect::R4_3: w = ratio(4, 3); break;
    case Aspect::R16_10: w = ratio(16, 10); break;
    case Aspect::R16_9: w = ratio(16, 9); break;
    case Aspect::R21_9: w = ratio(21, 9); break;
    case Aspect::Auto:
      if (clientW > 0 && clientH > 0) w = (int)((long long)H * clientW / clientH);
      break;
    default: break;
  }
  return w < base ? base : w > maxW ? maxW : w;
}

struct Viewport {
  int x, y, w, h;
};

Viewport computeViewport(int clientW, int clientH, int srcW, int srcH) {
  if (clientW <= 0 || clientH <= 0) return {0, 0, clientW, clientH};
  int w, h;
  if (g_settings.integerScaling && clientW >= srcW && clientH >= srcH) {
    int k = clientW / srcW < clientH / srcH ? clientW / srcW : clientH / srcH;
    w = srcW * k;
    h = srcH * k;
  } else if ((long long)clientW * srcH >= (long long)clientH * srcW) {
    h = clientH;
    w = (int)((long long)clientH * srcW / srcH);
  } else {
    w = clientW;
    h = (int)((long long)clientW * srcH / srcW);
  }
  return {(clientW - w) / 2, (clientH - h) / 2, w, h};
}

// 0xAARRGGBB (the renderer's format) -> the RGBA8888 bytes libnx expects.
inline uint32_t toFramebuffer(uint32_t c) {
  return (c & 0xFF00FF00u) | ((c & 0xFFu) << 16) | ((c >> 16) & 0xFFu);
}

std::vector<int> g_xmap, g_ymap;

void saveSettings() {
  FILE* f = std::fopen(configPath().c_str(), "w");
  if (!f) return;
  std::fprintf(f, "%d %d %d\n", (int)g_settings.aspect, g_settings.integerScaling ? 1 : 0, g_settings.fps);
  std::fclose(f);
}

}  // namespace

void loadDisplaySettings() {
  FILE* f = std::fopen(configPath().c_str(), "r");
  if (!f) return;
  int aspect = (int)Aspect::Auto, integer = 0, fps = 60;
  if (std::fscanf(f, "%d %d %d", &aspect, &integer, &fps) >= 1) {
    if (aspect >= 0 && aspect < (int)Aspect::Count) g_settings.aspect = (Aspect)aspect;
    g_settings.integerScaling = integer != 0;
    if (fps == 0 || fps == 60) g_settings.fps = fps;
  }
  std::fclose(f);
}

bool initDisplay() {
  NWindow* win = nwindowGetDefault();
  if (!win) return false;
  if (R_FAILED(framebufferCreate(&g_fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 2))) return false;
  framebufferMakeLinear(&g_fb);
  g_displayReady = true;
  return true;
}

void presentDisplay(const gow::Surface& s) {
  if (!g_displayReady || s.w <= 0 || s.h <= 0) return;

  u32 stride = 0;
  void* base = framebufferBegin(&g_fb, &stride);
  if (!base) return;

  const int clientW = 1280, clientH = 720;
  const Viewport v = computeViewport(clientW, clientH, s.w, s.h);

  // Black bars: fill the whole buffer once; the viewport is then overwritten. RGBA8888 black
  // with an opaque alpha is 0xFF000000 in the linear buffer (bytes 00 00 00 FF).
  uint32_t* fb = (uint32_t*)base;
  const size_t stridePixels = stride / 4;
  for (int y = 0; y < clientH; y++) {
    uint32_t* row = fb + (size_t)y * stridePixels;
    std::fill(row, row + clientW, 0xFF000000u);
  }

  if (v.w > 0 && v.h > 0) {
    g_xmap.resize((size_t)v.w);
    g_ymap.resize((size_t)v.h);
    for (int x = 0; x < v.w; x++) g_xmap[(size_t)x] = (int)((long long)x * s.w / v.w);
    for (int y = 0; y < v.h; y++) g_ymap[(size_t)y] = (int)((long long)y * s.h / v.h);

    for (int y = 0; y < v.h; y++) {
      const uint32_t* src = s.px.data() + (size_t)g_ymap[(size_t)y] * s.w;
      uint32_t* dst = fb + (size_t)(v.y + y) * stridePixels + v.x;
      for (int x = 0; x < v.w; x++) dst[x] = toFramebuffer(src[g_xmap[(size_t)x]]);
    }
  }

  framebufferEnd(&g_fb);
}

void shutdownDisplay() {
  if (!g_displayReady) return;
  framebufferClose(&g_fb);
  g_displayReady = false;
}

}  // namespace sw
}  // namespace gow

// ------------------------------------------------------------------ Port
// The three carousel rows inserted into the game's Options page. Row 0 is the aspect (logical
// canvas width), row 1 is Fit/Integer scaling, row 2 is the display rate. The generated Game code
// only calls these, so no generated file needs to change for a different console.
namespace gow {

String Port::label(int row) {
  if (row == 0) return String(u"Resolution: ") + String::fromUtf8(sw::aspectName(sw::g_settings.aspect));
  if (row == 1) return String(sw::g_settings.integerScaling ? u"Scaling: Integer" : u"Scaling: Fit");
  if (sw::g_settings.fps == 0) return String(u"FPS: Original");
  return String(u"FPS: ") + String::valueOf(sw::g_settings.fps);
}

void Port::change(int row, int dir) {
  if (row == 0) {
    int n = (int)sw::Aspect::Count;
    sw::g_settings.aspect = (sw::Aspect)(((int)sw::g_settings.aspect + dir + n) % n);
  } else if (row == 1) {
    sw::g_settings.integerScaling = !sw::g_settings.integerScaling;
  } else {
    static const int choices[] = {0, 60};
    int idx = sw::g_settings.fps == 0 ? 0 : 1;
    idx = (idx + dir + 2) % 2;
    sw::g_settings.fps = choices[idx];
  }
  sw::saveSettings();
}

int Port::displayFps() { return sw::g_settings.fps; }

int Port::viewWidth() { return sw::logicalWidth(1280, 720); }

// Same table surgery as the PC port: insert the three rows before the Options header and shift
// every stored page link at or beyond that index.
Arr<int> Port::extendMenu(const Arr<int>& t) {
  const int at = 22, n = 3;
  if (t.length() <= at) return t;
  Arr<int> o(t.length() + n);
  auto shifted = [&](int e) {
    int target = (e >> 8) & 0xFF;
    if (target >= at) e = (e & ~0xFF00) | (((target + n) & 0xFF) << 8);
    return e;
  };
  for (int i = 0; i < at; i++) o[i] = shifted(t[i]);
  o[at] = (13 << 26) | 0x3000000;
  o[at + 1] = (14 << 26) | 0x3000000;
  o[at + 2] = (15 << 26) | 0x3000000;
  for (int i = at; i < t.length(); i++) o[i + n] = shifted(t[i]);
  return o;
}

}  // namespace gow
