#pragma once

struct Device;

/**
 * Runs the terminal until the user exits. Must be called after module_stop(&lvgl_module), since it
 * drives the display and keyboards directly.
 * @param keyboards every keyboard device to read input from (built-in, USB, or both); caller owns
 * the array and any reference on each device for the whole call
 */
void runTerminal(struct Device* display, struct Device* const* keyboards, int keyboardCount, struct Device* touch);
