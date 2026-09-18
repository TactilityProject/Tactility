// SPDX-License-Identifier: Apache-2.0
#include "sdl_input.h"
#include "sdl_display.h"

#include <tactility/drivers/keyboard.h>

#include <SDL2/SDL.h>

#include <cstdlib>
#include <mutex>

namespace {

constexpr size_t KEY_QUEUE_CAPACITY = 32;

// Written by sdl_input_pump() on the real main thread, read by sdl_input_get_pointer_state()/
// sdl_input_pop_key()/sdl_input_has_queued_key() on the lvgl task.
std::mutex state_mutex;

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

// Mirrors LVGL's own lv_sdl_keyboard.c keycode_to_ctrl_key(): every key returns a real Unicode
// codepoint per KeyboardKeyData::key's contract - never an LV_KEY_* constant, even for keys with
// no ordinary character of their own (arrows, Tab/Shift+Tab as focus nav, Home/End), which use the
// CodePoint enum's standard Unicode symbols for the concept. lvgl-module's keyboard.cpp translates
// all of these back to LVGL's own sentinels (CODEPOINT_ESCAPE/BACKSPACE/DELETE already equal their
// LV_KEY_* counterpart numerically, so no translation is needed for those). Printable characters
// arrive separately via SDL_TEXTINPUT.
// The window can be freely resized (see sdl_display.cpp's SDL_WINDOW_RESIZABLE/
// SDL_RenderSetLogicalSize()), so raw SDL mouse coordinates are in window-pixel space, not the
// fixed logical resolution LVGL renders at. SDL_RenderWindowToLogical() is the renderer's own
// inverse of that scaling, accounting for both the scale factor and any letterbox offset.
void set_pointer_position(int32_t window_x, int32_t window_y) {
    SDL_Renderer* renderer = sdl_display_get_renderer();
    if (renderer == nullptr) {
        pointer_state.x = window_x;
        pointer_state.y = window_y;
        return;
    }
    float logical_x, logical_y;
    SDL_RenderWindowToLogical(renderer, window_x, window_y, &logical_x, &logical_y);
    pointer_state.x = static_cast<int32_t>(logical_x);
    pointer_state.y = static_cast<int32_t>(logical_y);
}

uint32_t keycode_to_key(SDL_Keycode sdl_key, bool shift) {
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
    // exit() must run with state_mutex unlocked: it never returns, so a lock_guard held across it
    // would never release the mutex, hanging any other thread that later calls into this file's
    // other functions (all of which lock state_mutex) while exit() tears the process down.
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
                        // SDL/X11 event-queue quirk - confirmed by comparing against a live
                        // SDL_GetWindowSize() at the same instant). SDL_GetMouseState() queries the
                        // OS for the current pointer position directly, sidestepping that entirely.
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
                case SDL_KEYDOWN:
                    push_key(keycode_to_key(event.key.keysym.sym, (event.key.keysym.mod & KMOD_SHIFT) != 0));
                    break;
                case SDL_TEXTINPUT:
                    // ASCII only (first byte of event.text.text) - sufficient for a simulator keyboard.
                    push_key(static_cast<uint8_t>(event.text.text[0]));
                    break;
                case SDL_WINDOWEVENT:
                    // Resizing doesn't change what LVGL last rendered, only how large it should
                    // appear - re-present the existing frame at the new scale immediately, rather
                    // than leaving stale-looking content on screen until the next LVGL-driven flush.
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
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
}

bool sdl_input_pop_key(uint32_t* out_key) {
    std::lock_guard<std::mutex> lock(state_mutex);
    if (key_queue_count == 0) {
        return false;
    }
    *out_key = key_queue[key_queue_head];
    key_queue_head = (key_queue_head + 1) % KEY_QUEUE_CAPACITY;
    key_queue_count--;
    return true;
}

bool sdl_input_has_queued_key() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return key_queue_count > 0;
}
