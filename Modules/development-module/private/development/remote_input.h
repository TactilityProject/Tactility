// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <development/protocol.h>

#include <stdint.h>

struct RemoteInput;

/** @return nullptr when out of memory or when the virtual input drivers can't be registered */
RemoteInput* remote_input_alloc();

/** Destroys all input devices that are still present */
void remote_input_free(RemoteInput* input);

/**
 * Creates the virtual input devices in @a mask and destroys the others.
 * @param[in] mask DevelopmentRemoteInput bits
 * @return false when a device could not be created
 */
bool remote_input_set_enabled(RemoteInput* input, uint8_t mask);

/** Ignored while the keyboard is disabled */
void remote_input_key(RemoteInput* input, uint32_t key, bool pressed, bool ctrl, bool alt);

/** The number of fingers that can touch at once */
#define REMOTE_INPUT_MAX_TOUCH_POINTS 5

struct RemoteTouchPoint {
    uint16_t x;
    uint16_t y;
};

/**
 * Sets the points that are touched now, which replaces the previous ones. A point that is no longer
 * listed was released. Coordinates are in pixels of the display. Ignored while the touch device is disabled.
 * @param[in] count at most REMOTE_INPUT_MAX_TOUCH_POINTS, 0 releases everything
 */
void remote_input_touch(RemoteInput* input, const RemoteTouchPoint* points, uint8_t count);
