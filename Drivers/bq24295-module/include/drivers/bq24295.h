// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct Bq24295Config {
    /** Address on bus */
    uint8_t address;
};

enum Bq24295WatchDogTimer {
    BQ24295_WATCHDOG_DISABLED = 0b000000,
    BQ24295_WATCHDOG_ENABLED_40S = 0b010000,
    BQ24295_WATCHDOG_ENABLED_80S = 0b100000,
    BQ24295_WATCHDOG_ENABLED_160S = 0b110000
};

error_t bq24295_get_watchdog_timer(struct Device* device, enum Bq24295WatchDogTimer* out);
error_t bq24295_set_watchdog_timer(struct Device* device, enum Bq24295WatchDogTimer in);

error_t bq24295_is_usb_power_connected(struct Device* device, bool* connected);

/** BATFET (battery FET): off disconnects the battery from the system (shipping mode). */
error_t bq24295_set_bat_fet_on(struct Device* device, bool on);

error_t bq24295_get_status(struct Device* device, uint8_t* value);
error_t bq24295_get_version(struct Device* device, uint8_t* value);

void bq24295_print_info(struct Device* device);

#ifdef __cplusplus
}
#endif
