// SPDX-License-Identifier: Apache-2.0
#include "sdl_input.h"
#include "sdl_display.h"

#include <tactility/drivers/keyboard.h>

#include <SDL2/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace {

constexpr size_t KEY_QUEUE_CAPACITY = 32;

std::mutex state_mutex;

// The position in window coordinates, and the size of the window then
SdlPointerState pointer_state = { 0, 0, false };
int window_width = 0;
int window_height = 0;

uint32_t key_queue[KEY_QUEUE_CAPACITY];
bool key_pressed_queue[KEY_QUEUE_CAPACITY] = {};
size_t key_queue_head = 0;
size_t key_queue_count = 0;

bool text_input_started = false;

struct HeldControlKey {
    uint32_t key = 0;
    uint8_t ref_count = 0;
};

constexpr size_t MAX_HELD_CONTROL_KEYS = 11;
HeldControlKey held_control_keys[MAX_HELD_CONTROL_KEYS] = {};

uint32_t last_reported_key = 0;
bool last_reported_pressed = false;

bool mark_control_key_held(uint32_t key) {
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.key == key) {
            slot.ref_count++;
            return false;
        }
    }
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.ref_count == 0) {
            slot.key = key;
            slot.ref_count = 1;
            return true;
        }
    }
    return true; // unreachable
}

bool clear_control_key_held(uint32_t key) {
    for (HeldControlKey& slot : held_control_keys) {
        if (slot.key == key) {
            slot.ref_count--;
            if (slot.ref_count == 0) {
                slot.key = 0;
                return true;
            }
            return false;
        }
    }
    return true;
}

uint32_t find_any_held_control_key() {
    for (const HeldControlKey& slot : held_control_keys) {
        if (slot.key != 0) {
            return slot.key;
        }
    }
    return 0;
}

void push_key(uint32_t key, bool pressed) {
    if (key == 0) {
        return;
    }
    if (key_queue_count >= KEY_QUEUE_CAPACITY) {
        size_t evict_offset = 0;
        for (size_t i = 0; i < key_queue_count; i++) {
            size_t idx = (key_queue_head + i) % KEY_QUEUE_CAPACITY;
            if (key_pressed_queue[idx]) {
                evict_offset = i;
                break;
            }
        }
        for (size_t i = evict_offset; i + 1 < key_queue_count; i++) {
            size_t dst = (key_queue_head + i) % KEY_QUEUE_CAPACITY;
            size_t src = (key_queue_head + i + 1) % KEY_QUEUE_CAPACITY;
            key_queue[dst] = key_queue[src];
            key_pressed_queue[dst] = key_pressed_queue[src];
        }
        key_queue_count--;
    }
    size_t idx = (key_queue_head + key_queue_count) % KEY_QUEUE_CAPACITY;
    key_queue[idx] = key;
    key_pressed_queue[idx] = pressed;
    key_queue_count++;
    last_reported_key = key;
    last_reported_pressed = pressed;

    if (!pressed) {
        uint32_t other_held_key = find_any_held_control_key();
        if (other_held_key != 0) {
            last_reported_key = other_held_key;
            last_reported_pressed = true;
        }
    }
}

void set_pointer_position(int32_t window_x, int32_t window_y) {
    pointer_state.x = window_x;
    pointer_state.y = window_y;
    SDL_Renderer* renderer = sdl_display_get_renderer();
    SDL_Window* window = renderer != nullptr ? SDL_RenderGetWindow(renderer) : nullptr;
    if (window != nullptr) {
        SDL_GetWindowSize(window, &window_width, &window_height);
    }
}

void map_to_view(int32_t* x, int32_t* y) {
    int view_width, view_height;
    if (window_width <= 0 || window_height <= 0 || !sdl_display_get_view_size(&view_width, &view_height) || view_width <= 0 || view_height <= 0) {
        return;
    }
    const float scale = std::min(static_cast<float>(window_width) / view_width, static_cast<float>(window_height) / view_height);
    const float offset_x = (window_width - view_width * scale) / 2.0f;
    const float offset_y = (window_height - view_height * scale) / 2.0f;
    *x = std::clamp(static_cast<int32_t>((*x - offset_x) / scale), 0, view_width - 1);
    *y = std::clamp(static_cast<int32_t>((*y - offset_y) / scale), 0, view_height - 1);
    sdl_display_view_to_native(x, y);
}

uint32_t keycode_to_key(SDL_Keycode sdl_key) {
    switch (sdl_key) {
        case SDLK_RIGHT: return CODEPOINT_ARROW_RIGHT;
        case SDLK_LEFT: return CODEPOINT_ARROW_LEFT;
        case SDLK_UP: return CODEPOINT_ARROW_UP;
        case SDLK_DOWN: return CODEPOINT_ARROW_DOWN;
        case SDLK_ESCAPE: return CODEPOINT_ESCAPE;
        case SDLK_BACKSPACE: return CODEPOINT_BACKSPACE;
        case SDLK_DELETE: return CODEPOINT_DELETE;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return CODEPOINT_ENTER;
        case SDLK_TAB: return CODEPOINT_TAB;
        case SDLK_HOME: return CODEPOINT_HOME;
        case SDLK_END: return CODEPOINT_END;
        default: return 0;
    }
}

}

void sdl_input_pump() {
    bool quit_requested = false;

    {
        std::lock_guard<std::mutex> lock(state_mutex);

        if (!text_input_started) {
            SDL_StartTextInput();
            text_input_started = true;
        }

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_MOUSEMOTION:
                    set_pointer_position(event.motion.x, event.motion.y);
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        // event.button.x/y can be stale immediately after a window resize (an
                        // SDL/X11 event-queue quirk)
                        int live_x, live_y;
                        SDL_GetMouseState(&live_x, &live_y);
                        set_pointer_position(live_x, live_y);
                        pointer_state.pressed = true;
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        pointer_state.pressed = false;
                    }
                    break;
                case SDL_KEYDOWN: {
                    // event.key.repeat is true for the auto-repeat events the OS sends while a
                    // key is held
                    if (event.key.repeat) {
                        break;
                    }
                    uint32_t key = keycode_to_key(event.key.keysym.sym);
                    if (key != 0 && mark_control_key_held(key)) {
                        push_key(key, true);
                    }
                    break;
                }
                case SDL_KEYUP: {
                    uint32_t key = keycode_to_key(event.key.keysym.sym);
                    if (key != 0 && clear_control_key_held(key)) {
                        push_key(key, false);
                    }
                    break;
                }
                case SDL_TEXTINPUT:
                    // ASCII only
                    if (static_cast<uint8_t>(event.text.text[0]) < 0x80) {
                        push_key(static_cast<uint8_t>(event.text.text[0]), true);
                        push_key(static_cast<uint8_t>(event.text.text[0]), false);
                    }
                    break;
                case SDL_WINDOWEVENT:
                    // Resizing doesn't change what LVGL last rendered, only how large it should
                    // appear so re-present the existing frame at the new scale immediately
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        window_width = event.window.data1;
                        window_height = event.window.data2;
                        sdl_display_present_now();
                    }
                    break;
                case SDL_QUIT:
                    quit_requested = true;
                    break;
                default:
                    break;
            }
        }
    }

    if (quit_requested) {
        exit(0);
    }
}

void sdl_input_get_pointer_state(SdlPointerState* out_state) {
    std::lock_guard<std::mutex> lock(state_mutex);
    *out_state = pointer_state;
    map_to_view(&out_state->x, &out_state->y);
}

bool sdl_input_pop_key(uint32_t* out_key, bool* out_pressed) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (key_queue_count == 0) {
        return false;
    }
    *out_key = key_queue[key_queue_head];
    *out_pressed = key_pressed_queue[key_queue_head];
    key_queue_head = (key_queue_head + 1) % KEY_QUEUE_CAPACITY;
    key_queue_count--;
    return true;
}

bool sdl_input_has_queued_key() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return key_queue_count > 0;
}

bool sdl_input_get_held_key(uint32_t* out_key) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (!last_reported_pressed || last_reported_key == 0) {
        return false;
    }
    for (const HeldControlKey& slot : held_control_keys) {
        if (slot.key == last_reported_key) {
            *out_key = slot.key;
            return true;
        }
    }
    return false;
}
