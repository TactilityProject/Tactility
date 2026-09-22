#pragma once

struct Device;

/**
 * Runs the terminal until the user exits. Must be called after module_stop(&lvgl_module), since it
 * drives the display and keyboards directly. Tracks keyboards and the touch-to-exit gesture itself.
 */
void runTerminal(struct Device* display);
