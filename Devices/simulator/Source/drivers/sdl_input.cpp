// SPDX-License-Identifier: Apache-2.0
#include "sdl_input.h"

#include <lvgl.h>

#include <SDL2/SDL.h>

#include <cstdlib>

namespace {

constexpr size_t KEY_QUEUE_CAPACITY = 32;

SdlPointerState pointer_state = { 0, 0, false };

uint32_t key_queue[KEY_QUEUE_CAPACITY];
size_t key_queue_head = 0;
size_t key_queue_count = 0;

bool text_input_started = false;

void push_key(uint32_t key) {
    if (key == 0 || key_queue_count >= KEY_QUEUE_CAPACITY) {
        return;
    }
    key_queue[(key_queue_head + key_queue_count) % KEY_QUEUE_CAPACITY] = key;
    key_queue_count++;
}

// Mirrors LVGL's own lv_sdl_keyboard.c keycode_to_ctrl_key(): maps navigation/control keys to
// LV_KEY_* constants. Printable characters arrive separately via SDL_TEXTINPUT.
uint32_t keycode_to_key(SDL_Keycode sdl_key) {
    switch (sdl_key) {
        case SDLK_RIGHT: return LV_KEY_RIGHT;
        case SDLK_LEFT: return LV_KEY_LEFT;
        case SDLK_UP: return LV_KEY_UP;
        case SDLK_DOWN: return LV_KEY_DOWN;
        case SDLK_ESCAPE: return LV_KEY_ESC;
        case SDLK_BACKSPACE: return LV_KEY_BACKSPACE;
        case SDLK_DELETE: return LV_KEY_DEL;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return LV_KEY_ENTER;
        case SDLK_TAB: return LV_KEY_NEXT;
        case SDLK_HOME: return LV_KEY_HOME;
        case SDLK_END: return LV_KEY_END;
        default: return 0;
    }
}

}

void sdl_input_pump() {
    if (!text_input_started) {
        SDL_StartTextInput();
        text_input_started = true;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_MOUSEMOTION:
                pointer_state.x = event.motion.x;
                pointer_state.y = event.motion.y;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    pointer_state.x = event.button.x;
                    pointer_state.y = event.button.y;
                    pointer_state.pressed = true;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    pointer_state.pressed = false;
                }
                break;
            case SDL_KEYDOWN:
                push_key(keycode_to_key(event.key.keysym.sym));
                break;
            case SDL_TEXTINPUT:
                // ASCII only (first byte of event.text.text) - sufficient for a simulator keyboard.
                push_key(static_cast<uint8_t>(event.text.text[0]));
                break;
            case SDL_QUIT:
                exit(0);
            default:
                break;
        }
    }
}

void sdl_input_get_pointer_state(SdlPointerState* out_state) {
    *out_state = pointer_state;
}

bool sdl_input_pop_key(uint32_t* out_key) {
    if (key_queue_count == 0) {
        return false;
    }
    *out_key = key_queue[key_queue_head];
    key_queue_head = (key_queue_head + 1) % KEY_QUEUE_CAPACITY;
    key_queue_count--;
    return true;
}

bool sdl_input_has_queued_key() {
    return key_queue_count > 0;
}
