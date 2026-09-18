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
 * @brief Pops the next queued key event (produced by SDL_KEYDOWN/SDL_TEXTINPUT during
 * sdl_input_pump()).
 * @retval false when no key event is pending
 */
bool sdl_input_pop_key(uint32_t* out_key);

/**
 * @brief Returns true if another key event is queued after the one just popped.
 */
bool sdl_input_has_queued_key(void);

#ifdef __cplusplus
}
#endif
