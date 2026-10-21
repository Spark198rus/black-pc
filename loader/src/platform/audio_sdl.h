// Platform SDL2 48 kHz Stereo Audio Output for Linux x86 (Phase 3)
#pragma once

#include <cstdint>

namespace audio_sdl {

constexpr uint32_t SAMPLE_RATE = 48000;
constexpr uint32_t CHANNELS    = 2;
constexpr uint32_t BLOCK_SIZE  = 480; // 10 ms at 48 kHz (interleaved L/R floats)
constexpr uint32_t TARGET_QUEUED_BLOCKS = 6; // 60 ms buffer ahead of device

bool open();
void close();

// Queries how many 10ms blocks the output pipeline can accept right now
uint32_t blocks_wanted();

// Submits one BLOCK of 480 stereo frames (960 float samples, interleaved L/R)
void submit(const float *lr_samples);

// Simulates lost audio device for testing error recovery
void simulate_lost();

// Diagnostics
bool is_open();
uint32_t queued_bytes();
uint64_t total_blocks_submitted();
uint32_t underrun_count();

} // namespace audio_sdl
