// Host audio output: one 48 kHz float stream (stereo, or 5.1: FL FR C LFE BL BR) fed by the DirectSound HLE mixer.
#pragma once

#include <cstdint>

namespace audio_out {

constexpr uint32_t RATE = 48000;
constexpr uint32_t BLOCK = 480; // frames per submitted block (10 ms)
constexpr uint32_t MAX_CHANNELS = 6;

bool open();                  // false: no host audio (blocks_wanted then follows the clock, silent)
uint32_t choose_channels();   // 2 or 6, decided once (the SPEAKERS setting, or the device for AUTO); call early
uint32_t channels();          // the same, after choose_channels()
uint32_t blocks_wanted();     // how many more blocks the device can queue now (by the clock while it has none)
void submit(const float *lr); // one BLOCK of interleaved samples, channels() per frame (copied)
void simulate_lost();         // tests: act as if the device had gone (XAudio2's critical error)

} // namespace audio_out
