// Host audio output: one 48 kHz stereo float stream fed by the DirectSound HLE mixer.
#pragma once

#include <cstdint>

namespace audio_out {

constexpr uint32_t RATE = 48000;
constexpr uint32_t BLOCK = 480; // frames per submitted block (10 ms)

bool open();                  // false: no host audio (blocks_wanted then follows the clock, silent)
uint32_t blocks_wanted();     // how many more blocks the device can queue now (by the clock while it has none)
void submit(const float *lr); // one BLOCK of interleaved L/R samples (copied)
void simulate_lost();         // tests: act as if the device had gone (XAudio2's critical error)

} // namespace audio_out
