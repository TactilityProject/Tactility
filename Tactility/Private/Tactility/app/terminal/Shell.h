#pragma once

#include <tactility/freertos/task.h>

/**
 * Launches the shell app and relays its stdio to/from vterm until it exits on its own or
 * `*stopRequested` asks it to (touch-to-exit); this is the same relationship a real terminal
 * emulator has to the shell it runs over a pty. Sets `*stopRequested` before returning either way,
 * so the caller's own I/O task loop (still running independently) winds down too.
 * `renderTask` is notified (xTaskNotifyGive) whenever shell output reaches vterm, so it can draw it right away.
 */
void runShell(int columns, int rows, volatile bool* stopRequested, TaskHandle_t renderTask);
