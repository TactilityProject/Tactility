// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bits of the value that DevelopmentRemoteUiHandlers::get_orientation returns */
#define DEVELOPMENT_REMOTE_ORIENTATION_ROTATION_MASK 0x03
/** The pixels that the display driver receives are rotated by the rotation, instead of the display doing that */
#define DEVELOPMENT_REMOTE_ORIENTATION_SOFTWARE 0x04

/**
 * Functions of the UI that remote sessions use. Every member is optional.
 */
struct DevelopmentRemoteUiHandlers {
    /** Makes the UI draw its whole screen, so that the first frames the viewer gets are complete. */
    void (*redraw)(void);

    /**
     * @return the rotation of the UI in quarter turns clockwise (0 to 3), with DEVELOPMENT_REMOTE_ORIENTATION_SOFTWARE
     * set when the display driver gets pixels that were rotated in software. Called regularly, as the UI can rotate at any time.
     * The viewer uses it to show the UI upright and to convert touch positions.
     */
    uint8_t (*get_orientation)(void);
};

/**
 * Installs the display hook that tracks the display state for remote sessions. Call it before the
 * UI configures a display, because a session can't ask a driver for state that was set later.
 * Safe to call more than once. The module installs it too when it starts.
 */
void development_remote_start(void);

/** Removes the display hook */
void development_remote_stop(void);

/** Sets the handlers of the UI. @param[in] handlers must stay valid, or NULL to remove them */
void development_remote_set_ui_handlers(const struct DevelopmentRemoteUiHandlers* handlers);

#ifdef __cplusplus
}
#endif
