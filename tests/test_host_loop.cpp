// Integration Test: Integrated Window, Input and Audio Loop for Linux x86
#include "../loader/src/platform/host_sdl.h"
#include "../loader/src/platform/input_sdl.h"
#include "../loader/src/platform/audio_sdl.h"

#include <SDL2/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

static void check(bool condition, const char *msg)
{
    if (!condition) {
        std::fprintf(stderr, "[FAIL host_loop] %s\n", msg);
        std::exit(1);
    }
}

int main(int argc, char **argv)
{
    bool interactive = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--gui") == 0 || std::strcmp(argv[i], "-g") == 0 ||
            std::strcmp(argv[i], "--interactive") == 0 || std::strcmp(argv[i], "-i") == 0) {
            interactive = true;
        }
    }

    std::printf("====================================================\n");
    if (interactive) {
        std::printf("=== Teste Interativo de Janela, Som e Input ===\n");
        std::printf("  - Mova o analógico ou use WASD / Mouse\n");
        std::printf("  - O tom de áudio mudará de frequência dinamicamente\n");
        std::printf("  - Pressione ESC ou feche a janela para finalizar\n");
    } else {
        std::printf("=== Running Host Loop Integration Test ====\n");
    }
    std::printf("====================================================\n");

    // 1. Initialize Window System
    bool win_ok = host_sdl::open_window(1280, 720, 16, 9, /*hidden=*/!interactive);
    check(win_ok, "Failed to initialize SDL2 host window");
    check(host_sdl::is_open(), "Host window is not open");
    std::printf("[host_loop] Window initialized successfully (1280x720 16:9 %s)\n",
                interactive ? "visível" : "hidden");

    // 2. Initialize Input System
    bool in_ok = input_sdl::init();
    check(in_ok, "Failed to initialize SDL2 input subsystem");
    std::printf("[host_loop] Input subsystem initialized successfully\n");

    // 3. Initialize Audio System
    bool audio_ok = audio_sdl::open();
    std::printf("[host_loop] Audio subsystem initialized: %s\n", audio_ok ? "hardware output" : "clock fallback");

    SDL_Renderer *renderer = nullptr;
    if (interactive) {
        renderer = SDL_CreateRenderer(host_sdl::window(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            renderer = SDL_CreateRenderer(host_sdl::window(), -1, SDL_RENDERER_SOFTWARE);
        }
    }

    if (interactive) {
        // Modo Interativo Livre: roda até fechar a janela ou apertar ESC / Back
        float phase = 0.0f;
        uint32_t frame_count = 0;

        while (!host_sdl::window_closed()) {
            host_sdl::pump_events();
            if (host_sdl::window_closed()) break;

            input_sdl::poll();

            XboxInputState pad0{};
            input_sdl::get_state(0, &pad0);

            // Se apertar ESC (Back) ou botão Start+Back, fecha
            if ((pad0.Gamepad.wButtons & XINPUT_BACK) && (pad0.Gamepad.wButtons & XINPUT_START)) {
                break;
            }

            // Calcula frequência de áudio modulada pelo analógico X (-32768..32767 -> 220Hz..880Hz)
            float freq = 440.0f + ((float)pad0.Gamepad.sThumbLX / 32768.0f) * 220.0f;
            if (freq < 150.0f) freq = 150.0f;

            // Gera bloco de áudio
            float audio_block[audio_sdl::BLOCK_SIZE * audio_sdl::CHANNELS];
            for (uint32_t i = 0; i < audio_sdl::BLOCK_SIZE; ++i) {
                float s = 0.08f * std::sin(phase);
                phase += 2.0f * 3.1415926535f * freq / (float)audio_sdl::SAMPLE_RATE;
                if (phase > 2.0f * 3.1415926535f) phase -= 2.0f * 3.1415926535f;

                audio_block[i * 2 + 0] = s;
                audio_block[i * 2 + 1] = s;
            }

            // Envia blocos demandados pelo mixer
            uint32_t wanted = audio_sdl::blocks_wanted();
            for (uint32_t b = 0; b < wanted; ++b) {
                audio_sdl::submit(audio_block);
            }

            // Renderiza fundo verde-esmeralda clássico do Xbox original
            if (renderer) {
                // Modula ligeiramente a cor com o gatilho direito (RT)
                uint8_t green_intensity = 48 + (pad0.Gamepad.bAnalogButtons[7] / 3);
                SDL_SetRenderDrawColor(renderer, 10, green_intensity, 18, 255);
                SDL_RenderClear(renderer);
                SDL_RenderPresent(renderer);
            }

            // Atualiza barra de título com diagnóstico em tempo real
            if ((frame_count % 15) == 0) {
                char title[256];
                std::snprintf(title, sizeof(title),
                              "BLACK PC | Stick:(%d, %d) LT:%u RT:%u Btn:0x%04X Freq:%.0fHz (ESC para sair)",
                              pad0.Gamepad.sThumbLX, pad0.Gamepad.sThumbLY,
                              pad0.Gamepad.bAnalogButtons[6], pad0.Gamepad.bAnalogButtons[7],
                              pad0.Gamepad.wButtons, freq);
                host_sdl::set_title(title);
            }

            frame_count++;
            usleep(16666);
        }

        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
        std::printf("[host_loop] Sessão interativa finalizada com sucesso pelo usuário.\n");
    } else {
        // Modo Automatizado de Teste (60 frames estritos)
        std::printf("[host_loop] Running 60-frame simulated game loop...\n");
        constexpr int TOTAL_FRAMES = 60;
        uint32_t frames_processed = 0;
        uint32_t audio_blocks_submitted = 0;

        float audio_block[audio_sdl::BLOCK_SIZE * audio_sdl::CHANNELS];
        for (uint32_t i = 0; i < audio_sdl::BLOCK_SIZE; ++i) {
            float s = 0.1f * std::sin(2.0f * 3.1415926535f * 440.0f * (float)i / (float)audio_sdl::SAMPLE_RATE);
            audio_block[i * 2 + 0] = s;
            audio_block[i * 2 + 1] = s;
        }

        for (int frame = 0; frame < TOTAL_FRAMES; ++frame) {
            host_sdl::pump_events();
            check(!host_sdl::window_closed(), "Window unexpectedly closed during loop");

            input_sdl::poll();

            XboxInputState pad0{};
            bool p0_connected = input_sdl::get_state(0, &pad0);
            check(p0_connected, "Player 0 must remain connected (KB/M fallback)");

            uint32_t wanted = audio_sdl::blocks_wanted();
            for (uint32_t b = 0; b < wanted; ++b) {
                audio_sdl::submit(audio_block);
                audio_blocks_submitted++;
            }

            frames_processed++;
            usleep(16666);
        }

        std::printf("[host_loop] Loop finished: %u frames processed, %u audio blocks submitted\n",
                    frames_processed, audio_blocks_submitted);

        check(frames_processed == TOTAL_FRAMES, "Did not complete all 60 frames");
        check(audio_blocks_submitted >= 40, "Audio stream starved (submitted less than 40 blocks)");
        check(audio_sdl::underrun_count() == 0, "Audio underrun detected during 60-frame run");

        int fw = 0, fh = 0, pw = 0, ph = 0;
        host_sdl::frame_layout(&fw, &fh, &pw, &ph);
        check(fw == 1280 && fh == 720, "Window dimensions corrupted during run");
        check(pw == 1280 && ph == 720, "Aspect ratio calculation mismatch");
    }

    // Clean shutdown
    audio_sdl::close();
    input_sdl::shutdown();
    host_sdl::close_window();

    check(!host_sdl::is_open(), "Window did not close cleanly");
    check(!audio_sdl::is_open(), "Audio did not close cleanly");

    std::printf("====================================================\n");
    std::printf("=== [PASS] Host Loop Fully Verified! ===============\n");
    std::printf("====================================================\n\n");
    return 0;
}
