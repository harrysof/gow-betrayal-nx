// Switch audio backend: a small General-MIDI-ish software synthesizer plus WAV playback, mixed to
// libnx's audout at 48 kHz stereo. The game only ever plays one Player at a time; the mixer still
// supports a MIDI sequence and a WAV clip together so state changes never click.
#include "platform.h"

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "switch/audio_switch.h"
#include "switch/switch_util.h"

namespace gow {

void audioLog(const std::string& line) { sw::appendLog(sw::join(sw::rootPath(), "audio.log"), line); }

// A MIDI file flattened for playback: channel messages in time order, timed in milliseconds.
struct MidiSequence {
  struct Event {
    double ms;
    uint32_t msg;  // status | data1 << 8 | data2 << 16
  };
  std::vector<Event> events;
  double lengthMs = 0;
  std::atomic<bool> playing{false};
};

struct WavClip {
  std::vector<int16_t> samples;  // interleaved; 1 or 2 channels
  int channels = 0;
  int sampleRate = 0;
  int frames = 0;
};

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kMaxVoices = 32;
constexpr int kBufferFrames = 1024;
constexpr int kBufferCount = 3;
constexpr int kBufferBytes = kBufferFrames * 2 * (int)sizeof(int16_t);

float g_sine[4096];

// ---------------------------------------------------------------- MIDI file
std::shared_ptr<MidiSequence> parseMidi(const Arr<int8_t>& d) {
  auto seq = std::make_shared<MidiSequence>();
  const uint8_t* p = (const uint8_t*)d.data();
  int n = d.length();
  auto u16 = [&](int o) { return (int)(p[o] << 8 | p[o + 1]); };
  auto u32 = [&](int o) { return (uint32_t)p[o] << 24 | (uint32_t)p[o + 1] << 16 | (uint32_t)p[o + 2] << 8 | p[o + 3]; };
  if (n < 14 || std::memcmp(p, "MThd", 4) != 0) return seq;
  int division = u16(12);
  double smpteTicksPerMs = 0;
  if (division & 0x8000) {
    smpteTicksPerMs = (256 - (division >> 8)) * (division & 0xFF) / 1000.0;
    if (smpteTicksPerMs <= 0) return seq;
  } else if (division == 0) {
    return seq;
  }

  struct Raw {
    int64_t tick;
    uint32_t msg;
  };
  std::vector<Raw> raw;
  std::vector<std::pair<int64_t, int>> tempos;
  int64_t endTick = 0;
  int o = 8 + (int)u32(4);
  while (o + 8 <= n) {
    uint32_t len = u32(o + 4);
    int start = o + 8;
    int end = (int)std::min<int64_t>((int64_t)start + len, n);
    if (std::memcmp(p + o, "MTrk", 4) == 0) {
      int q = start, status = 0;
      int64_t tick = 0;
      auto varLen = [&]() {
        int v = 0;
        for (int i = 0; i < 4 && q < end; i++) {
          int b = p[q++];
          v = v << 7 | (b & 0x7F);
          if (!(b & 0x80)) break;
        }
        return v;
      };
      while (q < end) {
        tick += varLen();
        if (q >= end) break;
        if (p[q] & 0x80) status = p[q++];
        if (status == 0xFF) {
          if (q >= end) break;
          int type = p[q++];
          int l = varLen();
          if (type == 0x51 && l == 3 && q + 3 <= end) tempos.push_back({tick, p[q] << 16 | p[q + 1] << 8 | p[q + 2]});
          q += l;
          status = 0;
          if (type == 0x2F) break;
        } else if (status == 0xF0 || status == 0xF7) {
          q += varLen();
          status = 0;
        } else if (status >= 0x80) {
          int hi = status & 0xF0;
          int count = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
          if (q + count > end) break;
          uint32_t msg = (uint32_t)status | (uint32_t)p[q] << 8 | (count == 2 ? (uint32_t)p[q + 1] << 16 : 0);
          raw.push_back({tick, msg});
          q += count;
        } else {
          break;
        }
      }
      endTick = std::max(endTick, tick);
    }
    o = start + (int)len;
  }

  std::stable_sort(raw.begin(), raw.end(), [](const Raw& a, const Raw& b) { return a.tick < b.tick; });
  std::stable_sort(tempos.begin(), tempos.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  size_t ti = 0;
  int64_t segTick = 0;
  double segMs = 0;
  int tempo = 500000;
  auto toMs = [&](int64_t tick) {
    if (smpteTicksPerMs > 0) return tick / smpteTicksPerMs;
    while (ti < tempos.size() && tempos[ti].first <= tick) {
      segMs += (double)(tempos[ti].first - segTick) * tempo / division / 1000.0;
      segTick = tempos[ti].first;
      tempo = tempos[ti].second;
      ti++;
    }
    return segMs + (double)(tick - segTick) * tempo / division / 1000.0;
  };
  seq->events.reserve(raw.size());
  for (const Raw& r : raw) seq->events.push_back({toMs(r.tick), r.msg});
  seq->lengthMs = toMs(endTick);
  if (!seq->events.empty()) seq->lengthMs = std::max(seq->lengthMs, seq->events.back().ms);
  return seq;
}

// ----------------------------------------------------------------- WAV file
std::shared_ptr<WavClip> parseWav(const Arr<int8_t>& d) {
  auto clip = std::make_shared<WavClip>();
  const uint8_t* p = (const uint8_t*)d.data();
  const int n = d.length();
  auto u16 = [&](int o) { return (int)(p[o] | p[o + 1] << 8); };
  auto u32 = [&](int o) { return (uint32_t)p[o] | (uint32_t)p[o + 1] << 8 | (uint32_t)p[o + 2] << 16 | (uint32_t)p[o + 3] << 24; };
  if (n < 44 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return clip;

  int format = 0, channels = 0, sampleRate = 0, bits = 0;
  int dataAt = -1;
  uint32_t dataLen = 0;
  for (int o = 12; o + 8 <= n;) {
    const uint32_t len = u32(o + 4);
    if (std::memcmp(p + o, "fmt ", 4) == 0 && o + 8 + 16 <= n) {
      format = u16(o + 8);
      channels = u16(o + 10);
      sampleRate = (int)u32(o + 12);
      bits = u16(o + 22);
    } else if (std::memcmp(p + o, "data", 4) == 0) {
      dataAt = o + 8;
      dataLen = len;
      break;
    }
    o += 8 + (int)len + (int)(len & 1);
  }
  if (format != 1 || (channels != 1 && channels != 2) || sampleRate <= 0 || (bits != 8 && bits != 16 && bits != 24 && bits != 32)) return clip;
  if (dataAt < 0) return clip;
  if ((uint32_t)dataAt + dataLen > (uint32_t)n) dataLen = (uint32_t)(n - dataAt);
  const int bytesPerSample = bits / 8;
  const int frameBytes = bytesPerSample * channels;
  if (frameBytes <= 0) return clip;

  clip->channels = channels;
  clip->sampleRate = sampleRate;
  clip->frames = (int)(dataLen / (uint32_t)frameBytes);
  clip->samples.resize((size_t)clip->frames * channels);
  for (int i = 0; i < clip->frames * channels; i++) {
    const uint8_t* s = p + dataAt + (size_t)i * bytesPerSample;
    switch (bits) {
      case 8: clip->samples[(size_t)i] = (int16_t)(((int)s[0] - 128) << 8); break;
      case 16: clip->samples[(size_t)i] = (int16_t)(s[0] | s[1] << 8); break;
      case 24: {
        int32_t v = s[0] | s[1] << 8 | s[2] << 16;
        if (v & 0x800000) v |= ~0xFFFFFF;
        clip->samples[(size_t)i] = (int16_t)(v >> 8);
        break;
      }
      default: {
        int32_t v = s[0] | s[1] << 8 | s[2] << 16 | s[3] << 24;
        clip->samples[(size_t)i] = (int16_t)(v >> 16);
        break;
      }
    }
  }
  return clip;
}

// ------------------------------------------------------------------- mixer
struct Channel {
  int program = 0;
  float volume = 1.0f;
  float expression = 1.0f;
  float pan = 0.5f;
  bool sustain = false;
  float bend = 1.0f;  // frequency multiplier
};

struct Voice {
  bool active = false;
  int channel = 0;
  int note = 0;
  int program = 0;
  int waveform = 0;  // 0 sine, 1 square, 2 saw, 3 triangle, 4 noise
  double phase = 0, phase2 = 0;
  double inc = 0, inc2 = 0;
  float env = 0, attack = 0, decay = 0, sustain = 0, release = 0;
  float releaseSec = 0.25f;
  int envState = 0;  // 0 attack, 1 decay, 2 sustain, 3 release
  float velocity = 1, gainL = 0, gainR = 0;
  double baseFreq = 440.0;
  bool keyDown = false, sustained = false;
  bool detune = false;
  bool drum = false;
  int drumType = 0;
  uint32_t lfsr = 0x12345678u;
  uint32_t age = 0;
};

Mutex g_lock;
bool g_lockReady = false;
bool g_audioReady = false;
std::atomic<bool> g_audioQuit{false};

Channel g_channels[16];
Voice g_voices[kMaxVoices];
uint32_t g_age = 0;

std::shared_ptr<MidiSequence> g_seq;
bool g_midiPlaying = false;
double g_songMs = 0;
size_t g_nextEvent = 0;
int g_loopsLeft = 1;

std::shared_ptr<WavClip> g_wav;
bool g_wavPlaying = false;
double g_wavPos = 0;
int g_wavLoopsLeft = 1;

float g_delayL[2400], g_delayR[2400];
int g_delayPos = 0;

int g_sampleRate = 48000;

alignas(0x1000) u8 g_audioData[kBufferCount][kBufferBytes];
AudioOutBuffer g_buffers[kBufferCount];
alignas(0x1000) u8 g_audioStack[0x20000];
Thread g_audioThread;
bool g_threadStarted = false;

void initSine() {
  for (int i = 0; i < 4096; i++) g_sine[i] = (float)std::sin(2.0 * kPi * i / 4096.0);
}

float wave(int type, double phase, Voice& v) {
  switch (type) {
    case 1: return phase < 0.5 ? 1.0f : -1.0f;
    case 2: return (float)(2.0 * phase - 1.0);
    case 3: return (float)(4.0 * std::fabs(phase - 0.5) - 1.0);
    case 4: {
      uint32_t x = v.lfsr;
      x ^= x << 13;
      x ^= x >> 17;
      x ^= x << 5;
      v.lfsr = x;
      return (float)((int32_t)x) / 2147483648.0f;
    }
    default: return g_sine[(int)(phase * 4096.0) & 4095];
  }
}

void setChannelGains(Voice& v) {
  const Channel& c = g_channels[v.channel];
  const float g = v.velocity * c.volume * c.expression;
  const float pan = c.pan < 0 ? 0 : c.pan > 1 ? 1 : c.pan;
  v.gainL = g * std::cos(pan * (float)kPi * 0.5f);
  v.gainR = g * std::sin(pan * (float)kPi * 0.5f);
}

void chooseInstrument(Voice& v) {
  const int p = v.program;
  v.detune = false;
  if (p < 8) { v.waveform = 3; v.attack = 0.004f; v.decay = 0.45f; v.sustain = 0.22f; v.release = 0.30f; }
  else if (p < 16) { v.waveform = 0; v.attack = 0.001f; v.decay = 0.35f; v.sustain = 0.0f; v.release = 0.20f; }
  else if (p < 24) { v.waveform = 1; v.attack = 0.01f; v.decay = 0.10f; v.sustain = 0.85f; v.release = 0.12f; }
  else if (p < 32) { v.waveform = 2; v.attack = 0.003f; v.decay = 0.22f; v.sustain = 0.12f; v.release = 0.22f; }
  else if (p < 40) { v.waveform = 3; v.attack = 0.005f; v.decay = 0.20f; v.sustain = 0.45f; v.release = 0.15f; }
  else if (p < 48) { v.waveform = 2; v.attack = 0.09f; v.decay = 0.20f; v.sustain = 0.80f; v.release = 0.45f; v.detune = true; }
  else if (p < 56) { v.waveform = 2; v.attack = 0.06f; v.decay = 0.20f; v.sustain = 0.80f; v.release = 0.40f; v.detune = true; }
  else if (p < 64) { v.waveform = 1; v.attack = 0.03f; v.decay = 0.15f; v.sustain = 0.72f; v.release = 0.22f; }
  else if (p < 72) { v.waveform = 1; v.attack = 0.02f; v.decay = 0.12f; v.sustain = 0.78f; v.release = 0.18f; }
  else if (p < 80) { v.waveform = 0; v.attack = 0.03f; v.decay = 0.10f; v.sustain = 0.85f; v.release = 0.20f; }
  else if (p < 88) { v.waveform = 2; v.attack = 0.01f; v.decay = 0.12f; v.sustain = 0.68f; v.release = 0.18f; }
  else if (p < 96) { v.waveform = 2; v.attack = 0.22f; v.decay = 0.25f; v.sustain = 0.78f; v.release = 0.55f; v.detune = true; }
  else if (p < 104) { v.waveform = 3; v.attack = 0.01f; v.decay = 0.30f; v.sustain = 0.35f; v.release = 0.30f; }
  else if (p < 112) { v.waveform = 3; v.attack = 0.004f; v.decay = 0.22f; v.sustain = 0.18f; v.release = 0.22f; }
  else if (p < 120) { v.waveform = 3; v.attack = 0.001f; v.decay = 0.16f; v.sustain = 0.0f; v.release = 0.12f; }
  else { v.waveform = 4; v.attack = 0.001f; v.decay = 0.22f; v.sustain = 0.0f; v.release = 0.12f; }
}

void startEnvelope(Voice& v) {
  v.env = 0;
  v.envState = 0;
  const float rate = (float)g_sampleRate;
  v.releaseSec = std::max(v.release, 0.001f);
  v.attack = v.attack > 0.0005f ? 1.0f / (v.attack * rate) : 1.0f;
  v.decay = v.sustain < 0.999f ? (1.0f - v.sustain) / (std::max(v.decay, 0.001f) * rate) : 1.0f;
}

Voice* allocVoice() {
  for (int i = 0; i < kMaxVoices; i++)
    if (!g_voices[i].active) return &g_voices[i];
  Voice* best = &g_voices[0];
  for (int i = 1; i < kMaxVoices; i++) {
    if (g_voices[i].env < best->env || (g_voices[i].env == best->env && g_voices[i].age < best->age)) best = &g_voices[i];
  }
  return best;
}

void startVoice(Voice& v, int ch, int note, int vel) {
  v.active = true;
  v.channel = ch;
  v.note = note;
  v.program = g_channels[ch].program;
  v.drum = ch == 9;
  v.drumType = 0;
  v.keyDown = true;
  v.sustained = false;
  v.age = ++g_age;
  v.phase = v.phase2 = 0;
  v.lfsr = 0x1234567u + (uint32_t)note * 2654435761u;
  v.velocity = std::pow(vel / 127.0f, 1.4f);

  const float freq = 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
  v.baseFreq = freq;
  if (v.drum) {
    v.env = 1;
    v.envState = 1;
    v.attack = 1;
    v.sustain = 0;
    v.release = (float)g_sampleRate;
    v.inc = freq * g_channels[ch].bend / g_sampleRate;
    v.inc2 = v.inc;
    v.waveform = 4;
    switch (note) {
      case 35: case 36:  // kick
        v.waveform = 0; v.inc = 120.0 / g_sampleRate; v.inc2 = v.inc; v.decay = 1.0f / (0.25f * g_sampleRate); v.drumType = 1; break;
      case 38: case 40:  // snare
        v.waveform = 4; v.decay = 1.0f / (0.18f * g_sampleRate); v.drumType = 2; break;
      case 42: case 44:  // closed hat
        v.waveform = 4; v.decay = 1.0f / (0.05f * g_sampleRate); v.drumType = 3; break;
      case 46:  // open hat
        v.waveform = 4; v.decay = 1.0f / (0.30f * g_sampleRate); v.drumType = 3; break;
      case 49: case 57:  // crash
        v.waveform = 4; v.decay = 1.0f / (0.80f * g_sampleRate); v.drumType = 4; break;
      case 51: case 59:  // ride
        v.waveform = 4; v.decay = 1.0f / (0.60f * g_sampleRate); v.drumType = 4; break;
      case 41: case 43: case 45: case 47: case 48: case 50:  // toms
        v.waveform = 0; v.inc = 180.0 / g_sampleRate; v.decay = 1.0f / (0.30f * g_sampleRate); v.drumType = 5; break;
      default:
        v.waveform = 4; v.decay = 1.0f / (0.20f * g_sampleRate); v.drumType = 6; break;
    }
  } else {
    chooseInstrument(v);
    v.inc = freq * g_channels[ch].bend / g_sampleRate;
    v.inc2 = v.inc * 1.006;
    startEnvelope(v);
  }
  setChannelGains(v);
}

void noteOn(int ch, int note, int vel) {
  if (vel <= 0) return;
  Voice* v = allocVoice();
  startVoice(*v, ch, note, vel);
}

void releaseVoice(Voice& v) {
  if (!v.active || v.envState == 3) return;
  if (v.drum) {
    v.release = std::max(v.env, 0.0001f);
  } else {
    v.release = v.env > 0.0001f ? v.env / (v.releaseSec * (float)g_sampleRate) : 1.0f;
  }
  v.envState = 3;
}

void noteOff(int ch, int note) {
  for (int i = 0; i < kMaxVoices; i++) {
    Voice& v = g_voices[i];
    if (v.active && v.channel == ch && v.note == note && v.keyDown && !v.drum) {
      v.keyDown = false;
      if (g_channels[ch].sustain) v.sustained = true;
      else releaseVoice(v);
    }
  }
}

void allNotesOff(int ch, bool immediate) {
  for (int i = 0; i < kMaxVoices; i++) {
    Voice& v = g_voices[i];
    if (!v.active || v.channel != ch) continue;
    if (immediate) v.active = false;
    else releaseVoice(v);
  }
}

void resetAll() {
  for (int i = 0; i < kMaxVoices; i++) g_voices[i].active = false;
  for (int c = 0; c < 16; c++) {
    g_channels[c].program = 0;
    g_channels[c].volume = 1.0f;
    g_channels[c].expression = 1.0f;
    g_channels[c].pan = 0.5f;
    g_channels[c].sustain = false;
    g_channels[c].bend = 1.0f;
  }
}

void updateChannelGains(int ch) {
  for (int i = 0; i < kMaxVoices; i++)
    if (g_voices[i].active && g_voices[i].channel == ch) setChannelGains(g_voices[i]);
}

void updateChannelPitch(int ch) {
  for (int i = 0; i < kMaxVoices; i++) {
    Voice& v = g_voices[i];
    if (v.active && v.channel == ch && !v.drum) {
      v.inc = v.baseFreq * g_channels[ch].bend / (double)g_sampleRate;
      v.inc2 = v.inc * 1.006;
    }
  }
}

void applyEvent(uint32_t msg) {
  const int status = (int)(msg & 0xFF);
  const int d1 = (int)((msg >> 8) & 0x7F);
  const int d2 = (int)((msg >> 16) & 0x7F);
  const int ch = status & 0x0F;
  if (ch > 15) return;
  switch (status & 0xF0) {
    case 0x80: noteOff(ch, d1); break;
    case 0x90:
      if (d2 == 0) noteOff(ch, d1);
      else noteOn(ch, d1, d2);
      break;
    case 0xA0: break;
    case 0xB0:
      switch (d1) {
        case 7: g_channels[ch].volume = d2 / 127.0f; updateChannelGains(ch); break;
        case 10: g_channels[ch].pan = d2 / 127.0f; updateChannelGains(ch); break;
        case 11: g_channels[ch].expression = d2 / 127.0f; updateChannelGains(ch); break;
        case 64:
          g_channels[ch].sustain = d2 >= 64;
          if (!g_channels[ch].sustain)
            for (int i = 0; i < kMaxVoices; i++)
              if (g_voices[i].active && g_voices[i].channel == ch && g_voices[i].sustained) {
                g_voices[i].sustained = false;
                releaseVoice(g_voices[i]);
              }
          break;
        case 120: allNotesOff(ch, true); break;
        case 121:
          g_channels[ch].volume = 1.0f;
          g_channels[ch].expression = 1.0f;
          g_channels[ch].pan = 0.5f;
          g_channels[ch].bend = 1.0f;
          g_channels[ch].sustain = false;
          updateChannelGains(ch);
          updateChannelPitch(ch);
          break;
        case 123: allNotesOff(ch, false); break;
        default: break;
      }
      break;
    case 0xC0: g_channels[ch].program = d1; break;
    case 0xD0: break;
    case 0xE0: {
      const int value = (d2 << 7) | d1;
      const float semis = (value - 8192) * (2.0f / 8192.0f);
      g_channels[ch].bend = std::pow(2.0f, semis / 12.0f);
      updateChannelPitch(ch);
      break;
    }
    default: break;
  }
}

void renderBlock(u8* out, int bytes) {
  const int frames = bytes / (2 * (int)sizeof(int16_t));
  static float mixL[kBufferFrames * 2], mixR[kBufferFrames * 2];
  const int maxFrames = (int)(sizeof mixL / sizeof mixL[0]);
  const int count = frames > maxFrames ? maxFrames : frames;
  for (int i = 0; i < count; i++) mixL[i] = mixR[i] = 0;

  const double msPerSample = 1000.0 / (double)g_sampleRate;

  for (int i = 0; i < count; i++) {
    // Advance MIDI song time and apply every event at this sample.
    if (g_midiPlaying && g_seq) {
      while (g_nextEvent < g_seq->events.size() && g_seq->events[g_nextEvent].ms <= g_songMs) {
        applyEvent(g_seq->events[g_nextEvent].msg);
        g_nextEvent++;
      }
      const double length = g_seq->lengthMs > 0.5 ? g_seq->lengthMs : 0.5;
      int guard = 0;
      while (g_songMs >= length && g_midiPlaying && guard++ < 8) {
        if (g_loopsLeft < 0) {
          g_songMs -= length;
          g_nextEvent = 0;
        } else if (--g_loopsLeft > 0) {
          g_songMs -= length;
          g_nextEvent = 0;
        } else {
          g_midiPlaying = false;
          if (g_seq) g_seq->playing = false;
          g_seq.reset();
          break;
        }
      }
    }

    float l = 0, r = 0;
    for (int vi = 0; vi < kMaxVoices; vi++) {
      Voice& v = g_voices[vi];
      if (!v.active) continue;

      if (v.envState == 0) {
        v.env += v.attack;
        if (v.env >= 1.0f) { v.env = 1.0f; v.envState = 1; }
      } else if (v.envState == 1) {
        v.env -= v.decay;
        if (v.env <= v.sustain) {
          v.env = v.sustain;
          v.envState = 2;
          if (v.sustain <= 0.0001f) { v.active = false; continue; }
        }
      } else if (v.envState == 3) {
        v.env -= v.release;
        if (v.env <= 0.0f) { v.active = false; continue; }
      }

      if (v.drum && v.drumType == 1) v.inc *= 0.9996;  // kick pitch drop
      if (v.drum && v.drumType == 5) v.inc *= 0.9994;  // tom pitch drop

      float s = wave(v.waveform, v.phase, v);
      if (v.detune) s = s * 0.65f + wave(v.waveform, v.phase2, v) * 0.35f;
      v.phase += v.inc;
      v.phase2 += v.inc2;
      if (v.phase >= 1.0) v.phase -= 1.0;
      if (v.phase2 >= 1.0) v.phase2 -= 1.0;

      l += s * v.env * v.gainL;
      r += s * v.env * v.gainR;
    }

    if (g_wavPlaying && g_wav && g_wav->frames > 0) {
      const WavClip& w = *g_wav;
      const size_t i0 = (size_t)g_wavPos;
      const size_t i1 = (i0 + 1 < (size_t)w.frames) ? i0 + 1 : (g_wavLoopsLeft < 0 ? 0 : (size_t)w.frames - 1);
      const float f = (float)(g_wavPos - (double)i0);
      auto sampleAt = [&](size_t frame, int ch) {
        const size_t idx = w.channels == 1 ? frame : frame * 2 + (size_t)ch;
        return w.samples[idx] / 32768.0f;
      };
      const float wl = sampleAt(i0, 0) * (1.0f - f) + sampleAt(i1, 0) * f;
      const float wr = w.channels == 1 ? wl : sampleAt(i0, 1) * (1.0f - f) + sampleAt(i1, 1) * f;
      l += wl * 0.65f;
      r += wr * 0.65f;
      g_wavPos += (double)w.sampleRate / (double)g_sampleRate;
      if (g_wavPos >= (double)w.frames) {
        if (g_wavLoopsLeft < 0 || --g_wavLoopsLeft > 0) {
          g_wavPos -= (double)w.frames;
        } else {
          g_wavPlaying = false;
          g_wav.reset();
        }
      }
    }

    // Small feedback delay to take the edge off the simple oscillators.
    {
      const float dl = g_delayL[g_delayPos], dr = g_delayR[g_delayPos];
      g_delayL[g_delayPos] = l + dr * 0.28f;
      g_delayR[g_delayPos] = r + dl * 0.28f;
      if (++g_delayPos >= 2400) g_delayPos = 0;
      l += dl * 0.18f;
      r += dr * 0.18f;
    }

    l *= 0.22f;
    r *= 0.22f;
    const int sl = (int)(l * 32767.0f);
    const int sr = (int)(r * 32767.0f);
    int16_t* dst = (int16_t*)(out + (size_t)i * 4);
    dst[0] = (int16_t)(sl < -32768 ? -32768 : sl > 32767 ? 32767 : sl);
    dst[1] = (int16_t)(sr < -32768 ? -32768 : sr > 32767 ? 32767 : sr);
    g_songMs += msPerSample;
  }

  // If the buffer was larger than the mixer's maximum, pad with silence.
  for (int i = count * 4; i < bytes; i++) out[i] = 0;
}

void audioThreadFunc(void*) {
  while (!g_audioQuit) {
    AudioOutBuffer* released = nullptr;
    u32 releasedCount = 0;
    Result rc = audoutWaitPlayFinish(&released, &releasedCount, 100000000ULL);
    if (R_FAILED(rc)) continue;
    while (released) {
      AudioOutBuffer* next = released->next;
      mutexLock(&g_lock);
      renderBlock((u8*)released->buffer, (int)released->buffer_size);
      mutexUnlock(&g_lock);
      released->data_size = released->buffer_size;
      released->next = nullptr;
      audoutAppendAudioOutBuffer(released);
      released = next;
    }
  }
}

}  // namespace

// ------------------------------------------------------------ public control
bool audioInit() {
  if (g_audioReady) return true;
  if (!g_lockReady) {
    mutexInit(&g_lock);
    g_lockReady = true;
  }
  initSine();
  resetAll();

  Result rc = audoutInitialize();
  if (R_FAILED(rc)) {
    audioLog("audoutInitialize failed: " + std::to_string(rc));
    return false;
  }
  rc = audoutStartAudioOut();
  if (R_FAILED(rc)) {
    audioLog("audoutStartAudioOut failed: " + std::to_string(rc));
    audoutExit();
    return false;
  }
  if (audoutGetChannelCount() != 2 || audoutGetPcmFormat() != PcmFormat_Int16) {
    audioLog("unexpected audout format (need stereo Int16)");
    audoutStopAudioOut();
    audoutExit();
    return false;
  }
  const u32 rate = audoutGetSampleRate();
  if (rate >= 8000 && rate <= 192000) g_sampleRate = (int)rate;

  for (int i = 0; i < kBufferCount; i++) {
    std::memset(g_audioData[i], 0, sizeof g_audioData[i]);
    std::memset(&g_buffers[i], 0, sizeof g_buffers[i]);
    g_buffers[i].next = nullptr;
    g_buffers[i].buffer = g_audioData[i];
    g_buffers[i].buffer_size = sizeof g_audioData[i];
    g_buffers[i].data_size = sizeof g_audioData[i];
    audoutAppendAudioOutBuffer(&g_buffers[i]);
  }

  g_audioQuit = false;
  if (R_FAILED(threadCreate(&g_audioThread, audioThreadFunc, nullptr, g_audioStack, sizeof g_audioStack, 0x20, -2))) {
    audioLog("audio threadCreate failed");
    audoutStopAudioOut();
    audoutExit();
    return false;
  }
  threadStart(&g_audioThread);
  g_threadStarted = true;
  g_audioReady = true;
  return true;
}

void audioShutdown() {
  if (!g_audioReady) return;
  g_audioQuit = true;
  audoutStopAudioOut();
  if (g_threadStarted) {
    threadWaitForExit(&g_audioThread);
    threadClose(&g_audioThread);
    g_threadStarted = false;
  }
  audoutExit();
  g_audioReady = false;
}

void audioPlayMidi(const std::shared_ptr<MidiSequence>& seq, int loops) {
  if (!g_audioReady || !seq) return;
  mutexLock(&g_lock);
  if (g_seq) g_seq->playing = false;
  resetAll();
  g_seq = seq;
  g_seq->playing = true;
  g_midiPlaying = true;
  g_songMs = 0;
  g_nextEvent = 0;
  g_loopsLeft = loops == 0 ? 1 : loops;
  mutexUnlock(&g_lock);
}

void audioStopMidi(const MidiSequence* seq) {
  if (!g_audioReady) return;
  mutexLock(&g_lock);
  if (g_seq && g_seq.get() == seq) {
    g_seq->playing = false;
    g_seq.reset();
    g_midiPlaying = false;
    for (int c = 0; c < 16; c++) allNotesOff(c, true);
  }
  mutexUnlock(&g_lock);
}

bool audioMidiPlaying(const MidiSequence* seq) {
  if (!g_audioReady) return false;
  mutexLock(&g_lock);
  const bool playing = g_midiPlaying && g_seq && g_seq.get() == seq;
  mutexUnlock(&g_lock);
  return playing;
}

void audioPlayWav(const std::shared_ptr<WavClip>& clip, int loops) {
  if (!g_audioReady || !clip) return;
  mutexLock(&g_lock);
  g_wav = clip;
  g_wavPlaying = true;
  g_wavPos = 0;
  g_wavLoopsLeft = loops == 0 ? 1 : loops;
  mutexUnlock(&g_lock);
}

void audioStopWav(const WavClip* clip) {
  if (!g_audioReady) return;
  mutexLock(&g_lock);
  if (g_wav && g_wav.get() == clip) {
    g_wav.reset();
    g_wavPlaying = false;
  }
  mutexUnlock(&g_lock);
}

bool audioWavPlaying(const WavClip* clip) {
  if (!g_audioReady) return false;
  mutexLock(&g_lock);
  const bool playing = g_wavPlaying && g_wav && g_wav.get() == clip;
  mutexUnlock(&g_lock);
  return playing;
}

// ------------------------------------------------------------------- Player
bool Player::isMidi() const { return mime.equals(String(u"audio/midi")); }

void Player::prefetch() {
  if (data == nullptr || seq_ || wav_) return;
  if (isMidi()) {
    seq_ = parseMidi(data);
    if (seq_->events.empty()) audioLog("midi not understood, bytes " + std::to_string(data.length()));
  } else {
    wav_ = parseWav(data);
    if (!wav_ || wav_->frames <= 0) audioLog("wav not understood, bytes " + std::to_string(data.length()));
  }
}

void Player::start() {
  if (data == nullptr) return;
  state_ = 400;
  if (isMidi()) {
    prefetch();
    if (seq_) audioPlayMidi(seq_, loops_);
  } else {
    prefetch();
    if (!wav_ || wav_->frames <= 0) {
      state_ = 300;  // unsupported/failed WAV: do not leave SoundPlayer waiting on it
      return;
    }
    audioPlayWav(wav_, loops_);
  }
}

void Player::stop() {
  state_ = 300;
  if (isMidi()) {
    if (seq_) audioStopMidi(seq_.get());
  } else if (wav_) {
    audioStopWav(wav_.get());
  }
}

void Player::close() {
  stop();
  state_ = 0;
  seq_.reset();
  wav_.reset();
}

void Player::pumpLoops() {}

int Player::getState() {
  if (state_ == 400) {
    if (isMidi()) {
      if (!seq_ || !audioMidiPlaying(seq_.get())) state_ = 300;
    } else if (!wav_ || !audioWavPlaying(wav_.get())) {
      state_ = 300;
    }
  }
  return state_;
}

}  // namespace gow
