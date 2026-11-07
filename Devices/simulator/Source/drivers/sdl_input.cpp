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

// Written by sdl_input_pump() on the real main thread, read by sdl_input_get_pointer_state()/
// sdl_input_pop_key()/sdl_input_has_queued_key() on the lvgl task.
std::mutex state_mutex;

// The position in window coordinates, and the size of the window then
SdlPointerState pointer_state = { 0, 0, false };
int window_width = 0;
int window_height = 0;

uint32_t key_queue[KEY_QUEUE_CAPACITY];
bool key_pressed_queue[KEY_QUEUE_CAPACITY] = {};
size_t key_queue_head = 0;
size_t key_queue_count = 0;

void push_key(uint32_t key, bool pressed) {
    if (key == 0 || key_queue_count >= KEY_QUEUE_CAPACITY) {
        return;
    }
    size_t idx = (key_queue_head + key_queue_count) % KEY_QUEUE_CAPACITY;
    key_queue[idx] = key;
    key_pressed_queue[idx] = pressed;
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
// resolution LVGL renders at. The view also changes shape when the display rotates. The raw
// position is kept with the window size, and sdl_input_get_pointer_state() maps it onto the
// current view, so a rotation takes effect right away even when the mouse doesn't move.
// LVGL rotates pointer positions from the display's native orientation to the rotated view itself,
// so the position in the view is turned back to a native one at the end.
// The view is scaled to fit the window and centered, like SDL_RenderSetLogicalSize() does.
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
        /* Letter keys mapped so SDL_KEYUP generates matching release events.
           SDL_TEXTINPUT is disabled below to avoid duplicate press events. */
        case SDLK_a: return shift ? 'A' : 'a';
        case SDLK_b: return shift ? 'B' : 'b';
        case SDLK_c: return shift ? 'C' : 'c';
        case SDLK_d: return shift ? 'D' : 'd';
        case SDLK_e: return shift ? 'E' : 'e';
        case SDLK_f: return shift ? 'F' : 'f';
        case SDLK_g: return shift ? 'G' : 'g';
        case SDLK_h: return shift ? 'H' : 'h';
        case SDLK_i: return shift ? 'I' : 'i';
        case SDLK_j: return shift ? 'J' : 'j';
        case SDLK_k: return shift ? 'K' : 'k';
        case SDLK_l: return shift ? 'L' : 'l';
        case SDLK_m: return shift ? 'M' : 'm';
        case SDLK_n: return shift ? 'N' : 'n';
        case SDLK_o: return shift ? 'O' : 'o';
        case SDLK_p: return shift ? 'P' : 'p';
        case SDLK_q: return shift ? 'Q' : 'q';
        case SDLK_r: return shift ? 'R' : 'r';
        case SDLK_s: return shift ? 'S' : 's';
        case SDLK_t: return shift ? 'T' : 't';
        case SDLK_u: return shift ? 'U' : 'u';
        case SDLK_v: return shift ? 'V' : 'v';
        case SDLK_w: return shift ? 'W' : 'w';
        case SDLK_x: return shift ? 'X' : 'x';
        case SDLK_y: return shift ? 'Y' : 'y';
        case SDLK_z: return shift ? 'Z' : 'z';
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
                    push_key(keycode_to_key(event.key.keysym.sym, (event.key.keysym.mod & KMOD_SHIFT) != 0), true);
                    break;
                case SDL_KEYUP:
                    push_key(keycode_to_key(event.key.keysym.sym, (event.key.keysym.mod & KMOD_SHIFT) != 0), false);
                    break;
                case SDL_WINDOWEVENT:
                    // Resizing doesn't change what LVGL last rendered, only how large it should
                    // appear - re-present the existing frame at the new scale immediately, rather
                    // than leaving stale-looking content on screen until the next LVGL-driven flush.
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
