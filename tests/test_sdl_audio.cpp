// Unit Test: SDL2 48 kHz Stereo Audio Output for Linux x86
#include "../loader/src/platform/audio_sdl.h"
#include "../loader/src/audio/output.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

static void check(bool condition, const char *msg)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL] %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    std::printf("=== Running test_sdl_audio ===\n");

    // 1. Open SDL audio
    bool opened = audio_sdl::open();
    std::printf("[test_sdl_audio] audio_sdl::open() returned: %s\n", opened ? "true" : "false");

    // 2. Synthesize 440 Hz stereo sine wave block (10 ms = 480 frames = 960 floats)
    constexpr uint32_t FRAMES = audio_sdl::BLOCK_SIZE;
    constexpr uint32_t CHANNELS = audio_sdl::CHANNELS;
    float block[FRAMES * CHANNELS];
    for (uint32_t i = 0; i < FRAMES; ++i) {
        float sample = 0.15f * std::sin(2.0f * 3.1415926535f * 440.0f * (float)i / (float)audio_sdl::SAMPLE_RATE);
        block[i * 2 + 0] = sample; // Left
        block[i * 2 + 1] = sample; // Right
    }

    if (opened) {
        check(audio_sdl::is_open(), "is_open() should be true after successful open");

        // Initial demand should be TARGET_QUEUED_BLOCKS (6 blocks = 60 ms)
        uint32_t initial_wanted = audio_sdl::blocks_wanted();
        std::printf("[test_sdl_audio] Initial blocks wanted: %u\n", initial_wanted);
        check(initial_wanted == audio_sdl::TARGET_QUEUED_BLOCKS, "Initial blocks wanted must equal TARGET_QUEUED_BLOCKS");

        // Submit initial wanted blocks
        for (uint32_t i = 0; i < initial_wanted; ++i) {
            audio_sdl::submit(block);
        }

        uint64_t total_sub = audio_sdl::total_blocks_submitted();
        check(total_sub == initial_wanted, "total_blocks_submitted mismatch");

        uint32_t queued = audio_sdl::queued_bytes();
        std::printf("[test_sdl_audio] Queued bytes after submission: %u\n", queued);
        constexpr uint32_t BYTES_PER_BLOCK = FRAMES * CHANNELS * sizeof(float);
        check(queued == initial_wanted * BYTES_PER_BLOCK, "Queued bytes mismatch with expected total");

        // Queue should now be satisfied
        uint32_t wanted_now = audio_sdl::blocks_wanted();
        check(wanted_now == 0, "blocks_wanted() should be 0 when queue is full");

        // Let audio stream play so hardware drains some queued data
        uint32_t queued_after_delay = queued;
        for (int retry = 0; retry < 10 && queued_after_delay >= queued; ++retry) {
            usleep(15000); // 15 ms per step, up to 150 ms total
            queued_after_delay = audio_sdl::queued_bytes();
        }
        std::printf("[test_sdl_audio] Queued bytes after playback: %u (initial: %u)\n", queued_after_delay, queued);
        check(queued_after_delay < queued, "Hardware device should have consumed audio bytes during sleep");

        // Test device invalidation simulation
        audio_sdl::simulate_lost();
        check(!audio_sdl::is_open(), "is_open() must be false after simulate_lost()");

        // In simulated lost state, blocks_wanted must continue advancing via clock fallback
        usleep(25000);
        uint32_t clock_wanted = audio_sdl::blocks_wanted();
        std::printf("[test_sdl_audio] Clock fallback wanted blocks: %u\n", clock_wanted);
        check(clock_wanted >= 1, "Clock fallback must request blocks when device is lost");

        audio_sdl::close();
        check(!audio_sdl::is_open(), "is_open() must be false after close()");
    } else {
        std::printf("[test_sdl_audio] (Note: Audio device not available in this environment; testing clock fallback)\n");
        check(!audio_sdl::is_open(), "is_open() must be false when open() failed");
        uint32_t want1 = audio_sdl::blocks_wanted();
        check(want1 >= 1, "Clock fallback must provide initial block request");
        usleep(25000);
        uint32_t want2 = audio_sdl::blocks_wanted();
        check(want2 >= 1, "Clock fallback must continue providing blocks over time");
        audio_sdl::close();
    }

    // 3. Test compatibility bridge functions with namespace audio_out
    bool bridge_open = audio_out::open();
    std::printf("[test_sdl_audio] audio_out::open() returned: %s\n", bridge_open ? "true" : "false");
    uint32_t bridge_wanted = audio_out::blocks_wanted();
    check(bridge_wanted > 0, "audio_out::blocks_wanted() must return > 0");
    audio_out::submit(block);
    audio_out::simulate_lost();
    usleep(20000);
    uint32_t bridge_lost_wanted = audio_out::blocks_wanted();
    check(bridge_lost_wanted >= 1, "audio_out::blocks_wanted() must advance via clock fallback after simulate_lost");

    audio_sdl::close();

    std::printf("[PASS] test_sdl_audio completed successfully with zero errors.\n\n");
    return 0;
}
