#pragma once

/**
 * Launches the shell app and relays its stdio to/from vterm until it exits on its own or
 * `*stopRequested` asks it to (touch-to-exit); this is the same relationship a real terminal
 * emulator has to the shell it runs over a pty. Sets `*stopRequested` before returning either way,
 * so the caller's own I/O task loop (still running independently) winds down too.
 */
void runShell(int columns, int rows, volatile bool* stopRequested);
