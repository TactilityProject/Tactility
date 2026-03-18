// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <tactility/error.h>

struct Device;

#ifdef __cplusplus
extern "C" {
#endif

struct M5pm1Config {
    uint8_t address;
};

// ---------------------------------------------------------------------------
// Power source (REG_PWR_SRC 0x04)
// ---------------------------------------------------------------------------
typedef enum {
    M5PM1_PWR_SRC_5VIN    = 0,
    M5PM1_PWR_SRC_5VINOUT = 1,
    M5PM1_PWR_SRC_BAT     = 2,
    M5PM1_PWR_SRC_UNKNOWN = 3,
} M5pm1PowerSource;

// ---------------------------------------------------------------------------
// Voltage readings
// ---------------------------------------------------------------------------
error_t m5pm1_get_battery_voltage(struct Device* device, uint16_t* mv);
error_t m5pm1_get_vin_voltage(struct Device* device, uint16_t* mv);
error_t m5pm1_get_5vout_voltage(struct Device* device, uint16_t* mv);
error_t m5pm1_get_power_source(struct Device* device, M5pm1PowerSource* source);

// ---------------------------------------------------------------------------
// Charging & power rails
// ---------------------------------------------------------------------------
/** PM1_G0 low = charging (connected to charge-status pin of the charge IC) */
error_t m5pm1_is_charging(struct Device* device, bool* charging);
error_t m5pm1_set_charge_enable(struct Device* device, bool enable);
error_t m5pm1_set_boost_enable(struct Device* device, bool enable);  ///< 5V BOOST / Grove power
error_t m5pm1_set_ldo_enable(struct Device* device, bool enable);    ///< 3.3V LDO

// ---------------------------------------------------------------------------
// Temperature (internal chip sensor)
// ---------------------------------------------------------------------------
/** Returns temperature in units of 0.1 °C */
error_t m5pm1_get_temperature(struct Device* device, uint16_t* decidegc);

// ---------------------------------------------------------------------------
// System commands
// ---------------------------------------------------------------------------
error_t m5pm1_shutdown(struct Device* device);
error_t m5pm1_reboot(struct Device* device);

// ---------------------------------------------------------------------------
// Power button (M5PM1 internal button, not ESP32 GPIO)
// ---------------------------------------------------------------------------
/** Current instantaneous state of the power button */
error_t m5pm1_btn_get_state(struct Device* device, bool* pressed);
/** Edge-triggered flag — auto-clears on read */
error_t m5pm1_btn_get_flag(struct Device* device, bool* was_pressed);

// ---------------------------------------------------------------------------
// Watchdog timer
// ---------------------------------------------------------------------------
/** timeout_sec: 0 = disabled, 1–255 = timeout in seconds */
error_t m5pm1_wdt_set(struct Device* device, uint8_t timeout_sec);
error_t m5pm1_wdt_feed(struct Device* device);

// ---------------------------------------------------------------------------
// RTC RAM (32 bytes, retained across sleep / power-off)
// ---------------------------------------------------------------------------
error_t m5pm1_read_rtc_ram(struct Device* device, uint8_t offset, uint8_t* data, uint8_t len);
error_t m5pm1_write_rtc_ram(struct Device* device, uint8_t offset, const uint8_t* data, uint8_t len);

// ---------------------------------------------------------------------------
// NeoPixel LED (via M5PM1 LED controller, max 32 LEDs)
// ---------------------------------------------------------------------------
error_t m5pm1_set_led_count(struct Device* device, uint8_t count);
error_t m5pm1_set_led_color(struct Device* device, uint8_t index, uint8_t r, uint8_t g, uint8_t b);
error_t m5pm1_refresh_leds(struct Device* device);
error_t m5pm1_disable_leds(struct Device* device);

#ifdef __cplusplus
}
#endif
