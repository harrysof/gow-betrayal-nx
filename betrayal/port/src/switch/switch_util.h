// Shared helpers for the Nintendo Switch backend (devkitA64 + libnx).
#pragma once

#include <switch.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#endif

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The host-side extraction test can point this at a temporary Windows folder.
#ifndef GOW_SWITCH_ROOT
#define GOW_SWITCH_ROOT "sdmc:/switch/gow-betrayal"
#endif

namespace gow {
namespace sw {

// Layout on the SD card:
//   sdmc:/switch/gow-betrayal/game.jar      the user's K800i .jar (extracted on first run)
//   sdmc:/switch/gow-betrayal/data/         RP1..RP33 (created from the jar)
//   sdmc:/switch/gow-betrayal/saves/        record-store files
//   sdmc:/switch/gow-betrayal/display.cfg   aspect/scaling/FPS settings
constexpr const char* kRoot = GOW_SWITCH_ROOT;

inline std::string rootPath() { return kRoot; }
inline std::string dataPath() { return std::string(kRoot) + "/data"; }
inline std::string savesPath() { return std::string(kRoot) + "/saves"; }
inline std::string join(const std::string& a, const char* b) { return a + "/" + b; }

// Monotonic milliseconds. The Switch's system tick runs at 19.2 MHz.
inline double nowMs() {
  return (double)armGetSystemTick() * 1000.0 / (double)armGetSystemTickFreq();
}

inline bool fileExists(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}

inline bool readFile(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < 0) {
    std::fclose(f);
    return false;
  }
  out.resize((size_t)n);
  size_t got = n ? std::fread(out.data(), 1, (size_t)n, f) : 0;
  std::fclose(f);
  return got == (size_t)n;
}

inline void ensureDir(const std::string& path) {
  // sd:/ is FAT; EEXIST is expected and harmless.
#if defined(_WIN32)
  _mkdir(path.c_str());
#else
  mkdir(path.c_str(), 0777);
#endif
}

inline void ensureDirs() {
#if !defined(_WIN32)
  ensureDir("sdmc:/switch");
#endif
  ensureDir(rootPath());
  ensureDir(dataPath());
  ensureDir(savesPath());
}

inline void appendLog(const std::string& path, const std::string& line) {
  FILE* f = std::fopen(path.c_str(), "a");
  if (!f) return;
  std::fprintf(f, "%s\n", line.c_str());
  std::fclose(f);
}

}  // namespace sw
}  // namespace gow
