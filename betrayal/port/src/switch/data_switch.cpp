#include "switch/data_switch.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <dirent.h>
#endif

#include "switch/switch_util.h"

extern "C" {
#include "puff.h"
}

namespace gow {
namespace sw {

namespace {

constexpr uint32_t kEndOfCentralDirectory = 0x06054b50;
constexpr uint32_t kCentralFileHeader = 0x02014b50;
constexpr uint32_t kLocalFileHeader = 0x04034b50;

uint16_t readU16(const std::vector<uint8_t>& d, size_t at) {
  if (at + 2 > d.size()) return 0;
  return (uint16_t)(d[at] | d[at + 1] << 8);
}

uint32_t readU32(const std::vector<uint8_t>& d, size_t at) {
  if (at + 4 > d.size()) return 0;
  return (uint32_t)d[at] | (uint32_t)d[at + 1] << 8 | (uint32_t)d[at + 2] << 16 | (uint32_t)d[at + 3] << 24;
}

bool readPrefix(const std::string& path, uint8_t* out, int n) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  size_t got = std::fread(out, 1, (size_t)n, f);
  std::fclose(f);
  return (int)got == n;
}

bool isResourceName(const std::string& name) {
  if (name.size() < 3 || name[0] != 'R' || name[1] != 'P') return false;
  for (size_t i = 2; i < name.size(); i++)
    if (!std::isdigit((unsigned char)name[i])) return false;
  return true;
}

bool hasMarkers(const std::string& dir) {
  return fileExists(join(dir, "RP1")) && fileExists(join(dir, "RP2")) && fileExists(join(dir, "RP3"));
}

// Lists .jar files in one directory. The app folder is on a FAT SD card, where "game.jar",
// "game.jar.jar" (Explorer's hidden extensions) and the original long filename all occur in
// practice; accepting any single jar avoids making the user rename it.
std::vector<std::string> findJarsIn(const std::string& dir) {
  std::vector<std::string> out;
#if !defined(_WIN32)
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (struct dirent* e = readdir(d)) {
    std::string name = e->d_name;
    if (name.size() < 4) continue;
    std::string lower = name;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower.substr(lower.size() - 4) == ".jar") out.push_back(join(dir, name.c_str()));
  }
  closedir(d);
#else
  (void)dir;
#endif
  return out;
}

std::vector<std::string> findJars() {
  std::vector<std::string> out = findJarsIn(rootPath());
  // A `game`/`Game` subfolder is a common way people copy a ROM set; look there too.
  std::vector<std::string> nested = findJarsIn(join(rootPath(), "game"));
  out.insert(out.end(), nested.begin(), nested.end());
  return out;
}

// Reads one resource out of an RP<n> bank (the format in ASSET_FORMATS.md). The atlas check
// below needs the raw bytes, not a decoded image, so it can read the PNG's IHDR directly.
bool readBankResource(const std::string& path, int index, std::vector<uint8_t>& out) {
  std::vector<uint8_t> d;
  if (!sw::readFile(path, d) || d.size() < 3) return false;
  const int lengthWidth = (d[0] & 1) ? 4 : 2;
  const int count = d[1] << 8 | d[2];
  if (index < 0 || index >= count) return false;
  size_t p = 3;
  if (p + (size_t)count * lengthWidth + (size_t)count + 4 > d.size()) return false;

  std::vector<uint32_t> lengths((size_t)count);
  for (int i = 0; i < count; i++) {
    uint32_t v = 0;
    for (int j = 0; j < lengthWidth; j++) v = v << 8 | d[p++];
    lengths[(size_t)i] = v;
  }
  p += (size_t)count;  // type bytes
  uint32_t payloadSize = 0;
  for (int j = 0; j < 4; j++) payloadSize = payloadSize << 8 | d[p++];
  if (p + payloadSize > d.size()) return false;

  size_t offset = 0;
  for (int i = 0; i < index; i++) offset += lengths[(size_t)i];
  if (offset + lengths[(size_t)index] > payloadSize) return false;
  out.assign(d.begin() + p + offset, d.begin() + p + offset + lengths[(size_t)index]);
  return true;
}

bool isPng(const std::vector<uint8_t>& d) {
  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  return d.size() >= 24 && std::memcmp(d.data(), sig, 8) == 0;
}

uint32_t readBe32(const std::vector<uint8_t>& d, size_t at) {
  return (uint32_t)d[at] << 24 | (uint32_t)d[at + 1] << 16 | (uint32_t)d[at + 2] << 8 | d[at + 3];
}

// The K800i build's 22 px tile atlas is id 2054 (RP2[6]); its image is RP2[10], a 176x352 PNG.
// Other v1.4.20 Sony Ericsson variants also have two RP19 resources but use 128x256 art, so the
// RP19 count alone is not enough to tell them apart. Keep the exact check here: the rest of this
// port's animation and level data is the K800i layout.
bool isK800i(const std::string& dir) {
  uint8_t header[3] = {};
  if (!readPrefix(join(dir, "RP19"), header, 3)) return false;
  if ((header[1] << 8 | header[2]) != 2) return false;

  std::vector<uint8_t> atlas, png;
  if (!readBankResource(join(dir, "RP2"), 6, atlas) || atlas.size() < 2) return false;
  const int imageId = atlas[0] << 8 | atlas[1];
  const int bank = imageId >> 10;
  const int index = imageId & 1023;
  const std::string bankName = "RP" + std::to_string(bank == 0 ? 2 : bank);
  if (!readBankResource(join(dir, bankName.c_str()), index, png) || !isPng(png)) return false;
  return readBe32(png, 16) == 176 && readBe32(png, 20) == 352;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& data) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = data.empty() || std::fwrite(data.data(), 1, data.size(), f) == data.size();
  std::fclose(f);
  return ok;
}

struct Entry {
  std::string name;
  uint16_t method = 0;
  uint16_t flags = 0;
  uint32_t compressedSize = 0;
  uint32_t uncompressedSize = 0;
  uint32_t localOffset = 0;
};

bool findEocd(const std::vector<uint8_t>& d, size_t& at) {
  if (d.size() < 22) return false;
  const size_t lowest = d.size() > 0xFFFF + 22 ? d.size() - 0xFFFF - 22 : 0;
  for (size_t candidate = d.size() - 22 + 1; candidate-- > lowest;)
    if (readU32(d, candidate) == kEndOfCentralDirectory) {
      at = candidate;
      return true;
    }
  return false;
}

bool parseCentralDirectory(const std::vector<uint8_t>& d, std::vector<Entry>& entries) {
  size_t eocd = 0;
  if (!findEocd(d, eocd)) return false;
  const uint16_t count = readU16(d, eocd + 10);
  const uint32_t directorySize = readU32(d, eocd + 12);
  const uint32_t directoryOffset = readU32(d, eocd + 16);
  if (directoryOffset > d.size() || (size_t)directoryOffset + directorySize > d.size()) return false;

  size_t at = directoryOffset;
  entries.clear();
  entries.reserve(count);
  for (uint16_t i = 0; i < count; i++) {
    if (at + 46 > d.size() || readU32(d, at) != kCentralFileHeader) return false;
    Entry e;
    e.flags = readU16(d, at + 8);
    e.method = readU16(d, at + 10);
    e.compressedSize = readU32(d, at + 20);
    e.uncompressedSize = readU32(d, at + 24);
    const uint16_t nameLength = readU16(d, at + 28);
    const uint16_t extraLength = readU16(d, at + 30);
    const uint16_t commentLength = readU16(d, at + 32);
    e.localOffset = readU32(d, at + 42);
    if (at + 46 + nameLength > d.size()) return false;
    e.name.assign((const char*)d.data() + at + 46, nameLength);
    entries.push_back(std::move(e));
    at += 46 + nameLength + extraLength + commentLength;
  }
  return true;
}

bool extractEntry(const std::vector<uint8_t>& zip, const Entry& e, const std::string& destDir, std::string& error) {
  if (e.flags & 1) {
    error = "'" + e.name + "' is encrypted";
    return false;
  }
  if (e.uncompressedSize > 64u * 1024u * 1024u) {
    error = "'" + e.name + "' is implausibly large";
    return false;
  }
  if (e.localOffset + 30 > zip.size() || readU32(zip, e.localOffset) != kLocalFileHeader) {
    error = "'" + e.name + "' has no local header";
    return false;
  }
  const uint16_t nameLength = readU16(zip, e.localOffset + 26);
  const uint16_t extraLength = readU16(zip, e.localOffset + 28);
  const size_t payload = (size_t)e.localOffset + 30 + nameLength + extraLength;
  if (payload + e.compressedSize > zip.size()) {
    error = "'" + e.name + "' runs past the end of the jar";
    return false;
  }

  std::vector<uint8_t> out;
  if (e.method == 0) {
    if (e.compressedSize != e.uncompressedSize) {
      error = "'" + e.name + "' is stored but its sizes disagree";
      return false;
    }
    out.assign(zip.begin() + payload, zip.begin() + payload + e.compressedSize);
  } else if (e.method == 8) {
    out.assign(e.uncompressedSize, 0);
    unsigned long destLen = e.uncompressedSize;
    unsigned long sourceLen = e.compressedSize;
    const int result = puff(out.data(), &destLen, zip.data() + payload, &sourceLen);
    if (result != 0 || destLen != e.uncompressedSize) {
      error = "'" + e.name + "' did not decompress cleanly";
      return false;
    }
  } else {
    error = "'" + e.name + "' uses an unsupported compression method";
    return false;
  }

  if (!writeFile(destDir + "/" + e.name, out)) {
    error = "could not write " + destDir + "/" + e.name;
    return false;
  }
  return true;
}

bool extractJar(const std::string& jarPath, const std::string& destDir, std::string& error) {
  std::vector<uint8_t> zip;
  if (!readFile(jarPath, zip)) {
    error = "could not read " + jarPath;
    return false;
  }
  if (zip.size() > 256u * 1024u * 1024u) {
    error = jarPath + " is too large";
    return false;
  }

  std::vector<Entry> entries;
  if (!parseCentralDirectory(zip, entries)) {
    error = jarPath + " is not a readable .jar/.zip";
    return false;
  }

  int written = 0;
  for (const Entry& e : entries) {
    if (!isResourceName(e.name)) continue;  // .class, META-INF and the icon are not needed
    if (!extractEntry(zip, e, destDir, error)) return false;
    written++;
  }
  if (written == 0) {
    error = jarPath + " does not contain the game's RP resource banks";
    return false;
  }
  return true;
}

}  // namespace

bool prepareGameData(std::string& error) {
  error.clear();
  ensureDirs();

#if !defined(_WIN32)
  // libnx mounts sdmc: before main() when the card is readable. If even the app folder cannot
  // be opened, report the card as missing instead of pretending the jar is just absent.
  DIR* root = opendir(rootPath().c_str());
  if (!root) {
    error =
        "Could not access sdmc:/switch/gow-betrayal.\n"
        "Is the SD card inserted and readable?";
    return false;
  }
  closedir(root);
#endif

  const bool hadData = hasMarkers(dataPath());
  if (hadData && isK800i(dataPath())) return true;

  static const char* const kJars[] = {
      "game.jar",
      "God-of-War-Betrayal_K800i_v1420.jar",
      "god_of_war_240x320.jar",
      "GodOfWar.jar",
  };
  std::string jar;
  for (const char* name : kJars) {
    const std::string candidate = join(rootPath(), name);
    if (fileExists(candidate)) {
      jar = candidate;
      break;
    }
  }
  if (jar.empty()) {
    const std::vector<std::string> found = findJars();
    if (found.size() == 1) {
      jar = found[0];  // any name works: game.jar.jar, the original (a3).jar filename, etc.
    } else if (!found.empty()) {
      error = "Several .jar files are in the app folder. Keep the K800i one and\nrename it to game.jar:\n";
      for (const std::string& path : found) {
        const size_t slash = path.find_last_of("/\\");
        error += "  " + path.substr(slash == std::string::npos ? 0 : slash + 1) + "\n";
      }
      return false;
    }
  }
  if (jar.empty()) {
    if (hadData) {
      error =
          "The data in sdmc:/switch/gow-betrayal/data is not the K800i 22 px build.\n"
          "Put the K800i .jar at sdmc:/switch/gow-betrayal/game.jar and start the app again.";
    } else {
      error =
          "No game data found. Copy your Sony Ericsson K800i God of War: Betrayal .jar to "
          "sdmc:/switch/gow-betrayal/game.jar and start the app again.";
    }
    return false;
  }

  if (!extractJar(jar, dataPath(), error)) return false;

  if (!hasMarkers(dataPath())) {
    error = "The jar unpacked, but RP1/RP2/RP3 are missing; it is not a God of War: Betrayal build.";
    return false;
  }
  if (!isK800i(dataPath())) {
    error =
        "That .jar is not the K800i 22 px build (MIDlet 1.4.20).\n"
        "Use the K800i jar; the other v1.4.20 Sony Ericsson variants use different art.";
    return false;
  }
  return true;
}

}  // namespace sw
}  // namespace gow
