// Platform SDL2 48 kHz Stereo Audio Output for Linux x86 (Phase 3)
#include "audio_sdl.h"
#include "audio/output.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace audio_sdl {
namespace {

constexpr uint32_t BYTES_PER_BLOCK = BLOCK_SIZE * CHANNELS * sizeof(float); // 480 * 2 * 4 = 3840 bytes

std::atomic<bool> g_is_open{false};
std::atomic<bool> g_simulated_lost{false};
SDL_AudioDeviceID g_device_id{0};

uint64_t g_total_submitted{0};
uint32_t g_underrun_count{0};
bool g_had_audio_queued{false};

// Clock fallback when audio device is unavailable, muted, or simulated lost
std::chrono::steady_clock::time_point g_clock_start;
uint64_t g_clock_blocks{0};
bool g_clock_active{false};

uint32_t clock_blocks()
{
    if (!g_clock_active) {
        g_clock_start = std::chrono::steady_clock::now();
        g_clock_blocks = 1;
        g_clock_active = true;
        return 1;
    }
    auto now = std::chrono::steady_clock::now();
    uint64_t elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(now - g_clock_start).count();
    uint64_t due = (elapsed_us * SAMPLE_RATE) / (BLOCK_SIZE * 1000000ULL) + 1;
    if (due > g_clock_blocks + (SAMPLE_RATE / BLOCK_SIZE)) {
        // Paused or slept: skip forward to avoid an unrealistic catchup burst
        g_clock_blocks = due - 1;
    }
    uint32_t want = (due > g_clock_blocks) ? (uint32_t)std::min<uint64_t>(due - g_clock_blocks, 10) : 0;
    g_clock_blocks += want;
    return want;
}

} // namespace

bool open()
{
    if (g_is_open && g_device_id != 0) {
        return true;
    }

    if (SDL_WasInit(SDL_INIT_AUDIO) == 0) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0) {
            std::fprintf(stderr, "[audio_sdl] SDL_InitSubSystem(AUDIO) failed: %s\n", SDL_GetError());
            return false;
        }
    }

    SDL_AudioSpec wanted{}, obtained{};
    wanted.freq = SAMPLE_RATE;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = CHANNELS;
    wanted.samples = BLOCK_SIZE;
    wanted.callback = nullptr; // Queue-based mode

    g_device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted, &obtained, 0);
    if (g_device_id == 0) {
        std::fprintf(stderr, "[audio_sdl] SDL_OpenAudioDevice failed: %s (using clock fallback)\n", SDL_GetError());
        g_is_open = false;
        return false;
    }

    SDL_PauseAudioDevice(g_device_id, 0); // Start playback
    g_is_open = true;
    g_simulated_lost = false;
    g_clock_active = false;
    g_had_audio_queued = false;

    return true;
}

void close()
{
    if (g_device_id != 0) {
        SDL_CloseAudioDevice(g_device_id);
        g_device_id = 0;
    }
    g_is_open = false;
    g_simulated_lost = false;
    g_clock_active = false;
    g_had_audio_queued = false;
}

uint32_t blocks_wanted()
{
    if (!g_is_open || g_simulated_lost || g_device_id == 0) {
        return clock_blocks();
    }

    uint32_t queued = SDL_GetQueuedAudioSize(g_device_id);
    uint32_t queued_blocks = queued / BYTES_PER_BLOCK;

    if (queued_blocks == 0 && g_had_audio_queued) {
        g_underrun_count++;
        g_had_audio_queued = false;
    } else if (queued_blocks > 0) {
        g_had_audio_queued = true;
    }

    if (queued_blocks < TARGET_QUEUED_BLOCKS) {
        return TARGET_QUEUED_BLOCKS - queued_blocks;
    }
    return 0;
}

void submit(const float *lr_samples)
{
    if (g_is_open && !g_simulated_lost && g_device_id != 0) {
        SDL_QueueAudio(g_device_id, lr_samples, BYTES_PER_BLOCK);
        g_had_audio_queued = true;
    }
    g_total_submitted++;
}

void simulate_lost()
{
    g_simulated_lost = true;
    g_clock_active = false;
}

bool is_open()
{
    return g_is_open && !g_simulated_lost && (g_device_id != 0);
}

uint32_t queued_bytes()
{
    if (!g_is_open || g_simulated_lost || g_device_id == 0) {
        return 0;
    }
    return SDL_GetQueuedAudioSize(g_device_id);
}

uint64_t total_blocks_submitted()
{
    return g_total_submitted;
}

uint32_t underrun_count()
{
    return g_underrun_count;
}

} // namespace audio_sdl

// Linkage compatibility for loader's audio_out namespace on Linux
namespace audio_out {

bool open()
{
    return audio_sdl::open();
}

uint32_t blocks_wanted()
{
    return audio_sdl::blocks_wanted();
}

void submit(const float *lr)
{
    audio_sdl::submit(lr);
}

void simulate_lost()
{
    audio_sdl::simulate_lost();
}

} // namespace audio_out
