// Platform SDL2 Gamepad, Keyboard and Mouse Input for Linux x86 (Phase 3)
#include "input_sdl.h"
#include "host_sdl.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace input_sdl {
namespace {

struct ControllerSlot {
    SDL_GameController *controller = nullptr;
    SDL_JoystickID      instance_id = -1;
    bool                connected = false;
    bool                synthetic_override = false;
    XboxInputState      state{};
};

ControllerSlot g_slots[4];
bool g_initialized = false;
float g_mouselook_yaw = 0.0f;
float g_mouselook_pitch = 0.0f;

int16_t apply_deadzone(int16_t val, int16_t deadzone)
{
    if (val > -deadzone && val < deadzone) return 0;
    return val;
}

} // namespace

bool init()
{
    if (g_initialized) return true;

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "input_sdl: Falha ao inicializar subsistema de controles SDL2: %s\n", SDL_GetError());
        return false;
    }

    for (auto &slot : g_slots) {
        slot = ControllerSlot{};
    }

    // Abrir quaisquer controladores físicos já conectados
    int num_joysticks = SDL_NumJoysticks();
    int slot_idx = 0;
    for (int i = 0; i < num_joysticks && slot_idx < 4; ++i) {
        if (SDL_IsGameController(i)) {
            SDL_GameController *gc = SDL_GameControllerOpen(i);
            if (gc) {
                SDL_Joystick *joy = SDL_GameControllerGetJoystick(gc);
                g_slots[slot_idx].controller = gc;
                g_slots[slot_idx].instance_id = SDL_JoystickInstanceID(joy);
                g_slots[slot_idx].connected = true;
                printf("[input_sdl] Controle %d conectado na porta %d: '%s'\n",
                       i, slot_idx, SDL_GameControllerName(gc));
                slot_idx++;
            }
        }
    }

    // A porta 0 é sempre considerada conectada (fallback para Teclado + Mouse se não houver gamepad)
    g_slots[0].connected = true;
    g_initialized = true;
    return true;
}

void shutdown()
{
    if (!g_initialized) return;

    for (int i = 0; i < 4; ++i) {
        if (g_slots[i].controller) {
            SDL_GameControllerClose(g_slots[i].controller);
            g_slots[i].controller = nullptr;
        }
        g_slots[i].connected = false;
    }

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    g_initialized = false;
}

void poll()
{
    if (!g_initialized) return;

    // Detectar eventos de hotplug do SDL
    SDL_Event ev;
    while (SDL_PeepEvents(&ev, 1, SDL_GETEVENT, SDL_CONTROLLERDEVICEADDED, SDL_CONTROLLERDEVICEREMOVED) > 0) {
        if (ev.type == SDL_CONTROLLERDEVICEADDED) {
            int which = ev.cdevice.which;
            for (int i = 0; i < 4; ++i) {
                if (!g_slots[i].controller) {
                    SDL_GameController *gc = SDL_GameControllerOpen(which);
                    if (gc) {
                        SDL_Joystick *joy = SDL_GameControllerGetJoystick(gc);
                        g_slots[i].controller = gc;
                        g_slots[i].instance_id = SDL_JoystickInstanceID(joy);
                        g_slots[i].connected = true;
                        printf("[input_sdl] Hotplug: controle conectado na porta %d: '%s'\n",
                               i, SDL_GameControllerName(gc));
                    }
                    break;
                }
            }
        } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
            SDL_JoystickID inst = ev.cdevice.which;
            for (int i = 0; i < 4; ++i) {
                if (g_slots[i].instance_id == inst) {
                    if (g_slots[i].controller) {
                        SDL_GameControllerClose(g_slots[i].controller);
                        g_slots[i].controller = nullptr;
                    }
                    g_slots[i].connected = (i == 0); // Porta 0 reverte para teclado/mouse
                    printf("[input_sdl] Hotplug: controle removido da porta %d\n", i);
                    break;
                }
            }
        }
    }

    // Atualizar cada slot
    for (int i = 0; i < 4; ++i) {
        if (g_slots[i].synthetic_override) {
            continue; // mantido por injeção direta de testes
        }

        XboxGamepadState prev_gamepad = g_slots[i].state.Gamepad;
        XboxGamepadState new_gamepad{};

        if (g_slots[i].controller) {
            SDL_GameController *gc = g_slots[i].controller;

            // D-Pad
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_UP))    new_gamepad.wButtons |= XINPUT_DPAD_UP;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  new_gamepad.wButtons |= XINPUT_DPAD_DOWN;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  new_gamepad.wButtons |= XINPUT_DPAD_LEFT;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) new_gamepad.wButtons |= XINPUT_DPAD_RIGHT;

            // Start e Back
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_START)) new_gamepad.wButtons |= XINPUT_START;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_BACK))  new_gamepad.wButtons |= XINPUT_BACK;

            // Thumbs
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSTICK))  new_gamepad.wButtons |= XINPUT_LEFT_THUMB;
            if (SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) new_gamepad.wButtons |= XINPUT_RIGHT_THUMB;

            // Botões de ação analógicos do Xbox (A, B, X, Y, Black, White)
            new_gamepad.bAnalogButtons[0] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_A) ? 255 : 0;
            new_gamepad.bAnalogButtons[1] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_B) ? 255 : 0;
            new_gamepad.bAnalogButtons[2] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_X) ? 255 : 0;
            new_gamepad.bAnalogButtons[3] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_Y) ? 255 : 0;
            new_gamepad.bAnalogButtons[4] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 255 : 0; // Black
            new_gamepad.bAnalogButtons[5] = SDL_GameControllerGetButton(gc, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 255 : 0;  // White

            // Gatilhos Analógicos (0..32767 -> 0..255)
            int16_t ltrig = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            int16_t rtrig = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
            new_gamepad.bAnalogButtons[6] = (ltrig > 0) ? (uint8_t)(ltrig * 255 / 32767) : 0;
            new_gamepad.bAnalogButtons[7] = (rtrig > 0) ? (uint8_t)(rtrig * 255 / 32767) : 0;

            // Analógicos com Deadzone (Inversão no eixo Y típica do Xbox)
            int16_t lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
            int16_t ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
            int16_t rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
            int16_t ry = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY);

            new_gamepad.sThumbLX = apply_deadzone(lx, 7849);
            new_gamepad.sThumbLY = (ly == -32768) ? 32767 : (int16_t)-apply_deadzone(ly, 7849);
            new_gamepad.sThumbRX = apply_deadzone(rx, 8689);
            new_gamepad.sThumbRY = (ry == -32768) ? 32767 : (int16_t)-apply_deadzone(ry, 8689);
        } else if (i == 0) {
            // Emulação por Teclado e Mouse para o Jogador 1
            const uint8_t *keys = SDL_GetKeyboardState(nullptr);
            if (keys) {
                // WASD -> Analógico Esquerdo
                int16_t k_lx = 0, k_ly = 0;
                if (keys[SDL_SCANCODE_W]) k_ly += 32767;
                if (keys[SDL_SCANCODE_S]) k_ly -= 32767;
                if (keys[SDL_SCANCODE_D]) k_lx += 32767;
                if (keys[SDL_SCANCODE_A]) k_lx -= 32767;
                new_gamepad.sThumbLX = k_lx;
                new_gamepad.sThumbLY = k_ly;

                // Teclas de Ação
                if (keys[SDL_SCANCODE_SPACE])    new_gamepad.bAnalogButtons[0] = 255; // A (Pulo)
                if (keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_LCTRL]) new_gamepad.bAnalogButtons[1] = 255; // B (Agachar)
                if (keys[SDL_SCANCODE_R])        new_gamepad.bAnalogButtons[2] = 255; // X (Recarregar)
                if (keys[SDL_SCANCODE_F] || keys[SDL_SCANCODE_E]) new_gamepad.bAnalogButtons[3] = 255; // Y (Interagir / Trocar arma)
                if (keys[SDL_SCANCODE_RETURN])   new_gamepad.wButtons |= XINPUT_START;
                if (keys[SDL_SCANCODE_ESCAPE])   new_gamepad.wButtons |= XINPUT_BACK;
                if (keys[SDL_SCANCODE_LSHIFT])   new_gamepad.wButtons |= XINPUT_LEFT_THUMB;
            }
        }

        // Se o estado mudou, incrementa o packet number
        if (memcmp(&prev_gamepad, &new_gamepad, sizeof(XboxGamepadState)) != 0) {
            g_slots[i].state.dwPacketNumber++;
            g_slots[i].state.Gamepad = new_gamepad;
        }
    }
}

bool get_state(uint32_t user_index, XboxInputState *out_state)
{
    if (user_index >= 4 || !g_slots[user_index].connected) {
        return false;
    }

    if (out_state) {
        *out_state = g_slots[user_index].state;
    }
    return true;
}

bool has_controller(uint32_t user_index)
{
    if (user_index >= 4) return false;
    return g_slots[user_index].controller != nullptr || g_slots[user_index].synthetic_override;
}

void set_rumble(uint32_t user_index, uint16_t left_motor, uint16_t right_motor)
{
    if (user_index < 4 && g_slots[user_index].controller) {
        SDL_GameControllerRumble(g_slots[user_index].controller, left_motor, right_motor, 100);
    }
}

void get_mouselook_delta(float *out_yaw_deg, float *out_pitch_deg)
{
    if (out_yaw_deg)   *out_yaw_deg = g_mouselook_yaw;
    if (out_pitch_deg) *out_pitch_deg = g_mouselook_pitch;
    g_mouselook_yaw = 0.0f;
    g_mouselook_pitch = 0.0f;
}

void inject_synthetic_state(uint32_t user_index, const XboxGamepadState &state)
{
    if (user_index >= 4) return;
    g_slots[user_index].connected = true;
    g_slots[user_index].synthetic_override = true;
    g_slots[user_index].state.dwPacketNumber++;
    g_slots[user_index].state.Gamepad = state;
}

} // namespace input_sdl

#include "platform/windefs.h"

extern "C" SHORT GetAsyncKeyState(int vk)
{
    const uint8_t *keys = SDL_GetKeyboardState(nullptr);
    if (!keys) return 0;

    if (vk == VK_LBUTTON) {
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_LEFT)) ? (SHORT)0x8000 : 0;
    }
    if (vk == VK_RBUTTON) {
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_RIGHT)) ? (SHORT)0x8000 : 0;
    }
    if (vk == VK_MBUTTON) {
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_MIDDLE)) ? (SHORT)0x8000 : 0;
    }
    if (vk == VK_XBUTTON1) {
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_X1)) ? (SHORT)0x8000 : 0;
    }
    if (vk == VK_XBUTTON2) {
        return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(SDL_BUTTON_X2)) ? (SHORT)0x8000 : 0;
    }

    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    if (vk >= 'A' && vk <= 'Z') sc = (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
    else if (vk >= '0' && vk <= '9') sc = (vk == '0') ? SDL_SCANCODE_0 : (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
    else if (vk >= VK_F1 && vk <= VK_F12) sc = (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - VK_F1));
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) sc = (vk == VK_NUMPAD0) ? SDL_SCANCODE_KP_0 : (SDL_Scancode)(SDL_SCANCODE_KP_1 + (vk - VK_NUMPAD0 - 1));
    else {
        switch (vk) {
            case VK_RETURN: sc = SDL_SCANCODE_RETURN; break;
            case VK_ESCAPE: sc = SDL_SCANCODE_ESCAPE; break;
            case VK_BACK: sc = SDL_SCANCODE_BACKSPACE; break;
            case VK_TAB: sc = SDL_SCANCODE_TAB; break;
            case VK_SPACE: sc = SDL_SCANCODE_SPACE; break;
            case VK_UP: sc = SDL_SCANCODE_UP; break;
            case VK_DOWN: sc = SDL_SCANCODE_DOWN; break;
            case VK_LEFT: sc = SDL_SCANCODE_LEFT; break;
            case VK_RIGHT: sc = SDL_SCANCODE_RIGHT; break;
            case VK_SHIFT:
            case VK_LSHIFT: sc = SDL_SCANCODE_LSHIFT; break;
            case VK_RSHIFT: sc = SDL_SCANCODE_RSHIFT; break;
            case VK_CONTROL:
            case VK_LCONTROL: sc = SDL_SCANCODE_LCTRL; break;
            case VK_RCONTROL: sc = SDL_SCANCODE_RCTRL; break;
            case VK_MENU:
            case VK_LMENU: sc = SDL_SCANCODE_LALT; break;
            case VK_RMENU: sc = SDL_SCANCODE_RALT; break;
            case VK_DELETE: sc = SDL_SCANCODE_DELETE; break;
            case VK_INSERT: sc = SDL_SCANCODE_INSERT; break;
            case VK_HOME: sc = SDL_SCANCODE_HOME; break;
            case VK_END: sc = SDL_SCANCODE_END; break;
            case VK_PRIOR: sc = SDL_SCANCODE_PAGEUP; break;
            case VK_NEXT: sc = SDL_SCANCODE_PAGEDOWN; break;
            case VK_CAPITAL: sc = SDL_SCANCODE_CAPSLOCK; break;
            case VK_PAUSE: sc = SDL_SCANCODE_PAUSE; break;
            case VK_SCROLL: sc = SDL_SCANCODE_SCROLLLOCK; break;
            case VK_NUMLOCK: sc = SDL_SCANCODE_NUMLOCKCLEAR; break;
            case VK_MULTIPLY: sc = SDL_SCANCODE_KP_MULTIPLY; break;
            case VK_ADD: sc = SDL_SCANCODE_KP_PLUS; break;
            case VK_SUBTRACT: sc = SDL_SCANCODE_KP_MINUS; break;
            case VK_DECIMAL: sc = SDL_SCANCODE_KP_PERIOD; break;
            case VK_DIVIDE: sc = SDL_SCANCODE_KP_DIVIDE; break;
            default: break;
        }
    }
    if (sc != SDL_SCANCODE_UNKNOWN && keys[sc]) {
        return (SHORT)0x8000;
    }
    if (vk == VK_SHIFT && keys[SDL_SCANCODE_RSHIFT]) return (SHORT)0x8000;
    if (vk == VK_CONTROL && keys[SDL_SCANCODE_RCTRL]) return (SHORT)0x8000;
    if (vk == VK_MENU && keys[SDL_SCANCODE_RALT]) return (SHORT)0x8000;

    return 0;
}
