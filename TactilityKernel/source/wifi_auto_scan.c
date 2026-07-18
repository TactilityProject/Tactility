// SPDX-License-Identifier: Apache-2.0
#include <tactility/wifi_auto_scan.h>
#include <stddef.h>

static void (*registeredSetPaused)(bool paused) = NULL;

void wifi_auto_scan_register(void (*set_paused)(bool paused)) {
    registeredSetPaused = set_paused;
}

void wifi_auto_scan_set_paused(bool paused) {
    if (registeredSetPaused != NULL) {
        registeredSetPaused(paused);
    }
}
