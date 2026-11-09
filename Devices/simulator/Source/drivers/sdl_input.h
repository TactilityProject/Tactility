// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Latest pointer (mouse) state as tracked by sdl_input_pump().
 */
struct SdlPointerState {
    int32_t x;
    int32_t y;
    bool pressed;
};

/**
 * @brief Drains all pending SDL events exactly once, updating the pointer state and key queue
 * below. Must be called only from the real OS main thread (sdl_bridge_run_main_loop()): SDL
 * requires event pumping to happen there on macOS. The getters below are safe to call from a
 * different thread (the lvgl task, via sdl-pointer/sdl-keyboard's polling functions).
 */
void sdl_input_pump(void);

/**
 * @brief Gets the pointer state as of the most recent sdl_input_pump() call.
 */
void sdl_input_get_pointer_state(struct SdlPointerState* out_state);

/**
 * @brief Pops the next queued key event (produced by SDL_KEYDOWN/SDL_KEYUP/SDL_TEXTINPUT during
 * sdl_input_pump()).
 * @param[out] out_key the key code
 * @param[out] out_pressed true for press, false for release
 * @retval false when no key event is pending
 */
bool sdl_input_pop_key(uint32_t* out_key, bool* out_pressed);

/**
 * @brief Returns true if another key event is queued after the one just popped.
 */
bool sdl_input_has_queued_key(void);

/**
 * @brief Gets one of the non-printable control keys (arrow/Enter/Backspace/.../see
 * sdl_input.cpp's keycode_to_key()) that is currently physically held down, if any.
 *
 * Used to keep reporting PRESSED on an idle poll between a control key's SDL_KEYDOWN and its
 * eventual SDL_KEYUP, once sdl_input_pop_key() has drained the queued press event itself.
 * Printable keys (letters, digits, space, punctuation) are never held this way - see
 * SDL_TEXTINPUT handling in sdl_input_pump(). If several control keys are held at once, an
 * arbitrary one of them is returned - matches LVGL's own keypad indev model of a single active
 * key.
 * @param[out] out_key the held key code
 * @retval false when no control key is currently held
 */
bool sdl_input_get_held_key(uint32_t* out_key);

#ifdef __cplusplus
}
#endif
