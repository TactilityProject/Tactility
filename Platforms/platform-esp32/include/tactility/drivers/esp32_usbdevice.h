// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32UsbDeviceConfig {
    int _unused;
};

struct Esp32UsbDeviceChildConfig {
    int _unused;
    /**
     * CDC only: install the CDC-ACM console at boot with no primary class claiming the
     * USB device slot. Boards whose normal boot path has no active MSC/HID/MIDI (for
     * example a board with no SD card) use this to expose a serial console/log port.
     * Ignored by the HID/MSC/MIDI bindings.
     */
    bool console_at_boot;
};

#ifdef __cplusplus
}
#endif
