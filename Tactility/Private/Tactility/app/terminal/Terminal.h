#pragma once

struct Device;
class TerminalRenderer;

/**
 * Runs the terminal until the user exits. Unless the renderer is a TerminalRendererLvgl, must be
 * called after module_stop(&lvgl_module), since it then drives the display directly. Tracks
 * keyboards itself.
 * @param[in] renderer not yet begun, begun and ended by this call
 * @param[in] touchToExit whether touching the screen exits a keyboard-less terminal
 */
void runTerminal(struct Device* display, TerminalRenderer& renderer, bool touchToExit);
