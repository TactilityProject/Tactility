// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include <tactility/drivers/keyboard.h>

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
 * @param[out] out_data the key, whether it was pressed, and the modifiers of its press. continue_reading is not set.
 * @retval false when no key event is pending
 */
bool sdl_input_pop_key(struct KeyboardKeyData* out_data);

/**
 * @brief Returns true if another key event is queued after the one just popped.
 */
bool sdl_input_has_queued_key(void);

/**
 * @brief Gets the non-printable control key (arrow/Enter/Backspace/.../see sdl_input.cpp's
 * keycode_to_key()) that LVGL last saw reported as pressed via sdl_input_pop_key(), if it is
 * still physically held down
 *
 * @param[out] out_data the held key as it was pressed
 * @retval false when no control key is currently held, or the one last reported was released
 */
bool sdl_input_get_held_key(struct KeyboardKeyData* out_data);

#ifdef __cplusplus
}
#endif
