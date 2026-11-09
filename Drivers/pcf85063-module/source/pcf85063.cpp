// SPDX-License-Identifier: Apache-2.0
#include <drivers/pcf85063.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "PCF85063"

static constexpr uint8_t REG_CONTROL1 = 0x00;
// Seconds to years in 0x04 to 0x0A, bit 7 of the seconds is the oscillator stop flag
static constexpr uint8_t REG_SECONDS = 0x04;

static constexpr uint8_t CONTROL1_STOP = 1U << 5;
static constexpr uint8_t CONTROL1_12_HOUR = 1U << 1;
static constexpr uint8_t SECONDS_OSCILLATOR_STOPPED = 1U << 7;

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(50);

#define GET_CONFIG(device) (static_cast<const Pcf85063Config*>((device)->config))

static uint8_t bcd_to_dec(uint8_t bcd) { return static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F)); }
static uint8_t dec_to_bcd(uint8_t dec) { return static_cast<uint8_t>(((dec / 10) << 4) | (dec % 10)); }

static error_t start(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Runs the clock in 24 hour mode and keeps the oscillator load setting
    if (i2c_controller_register8_reset_bits(i2c_controller, address, REG_CONTROL1, CONTROL1_STOP | CONTROL1_12_HOUR, I2C_TIMEOUT_TICKS) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start the clock at 0x%02X", address);
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t stop(Device*) {
    // The clock keeps running on the backup supply
    return ERROR_NONE;
}

extern "C" {

error_t pcf85063_get_datetime(Device* device, RtcDateTime* dt) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // seconds, minutes, hours, days, weekdays, months, years
    uint8_t buf[7] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_SECONDS, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    if (buf[0] & SECONDS_OSCILLATOR_STOPPED) {
        LOG_W(TAG, "Oscillator stopped since the time was set");
        return ERROR_INVALID_STATE;
    }
    dt->second = bcd_to_dec(buf[0] & 0x7Fu);
    dt->minute = bcd_to_dec(buf[1] & 0x7Fu);
    dt->hour = bcd_to_dec(buf[2] & 0x3Fu);
    dt->day = bcd_to_dec(buf[3] & 0x3Fu);
    dt->month = bcd_to_dec(buf[5] & 0x1Fu);
    dt->year = static_cast<uint16_t>(2000 + bcd_to_dec(buf[6]));

    return ERROR_NONE;
}

error_t pcf85063_set_datetime(Device* device, const RtcDateTime* dt) {
    if (dt->year < 2000 || dt->year > 2099 ||
        dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > 31 ||
        dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // Writing the seconds also clears the oscillator stop flag
    uint8_t buf[7] = {};
    buf[0] = dec_to_bcd(dt->second);
    buf[1] = dec_to_bcd(dt->minute);
    buf[2] = dec_to_bcd(dt->hour);
    buf[3] = dec_to_bcd(dt->day);
    buf[4] = 0;
    buf[5] = dec_to_bcd(dt->month);
    buf[6] = dec_to_bcd(static_cast<uint8_t>(dt->year - 2000));

    return i2c_controller_write_register(i2c_controller, address, REG_SECONDS, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
}

RtcApi pcf85063_rtc_api = {
    .get_time = pcf85063_get_datetime,
    .set_time = pcf85063_set_datetime
};

Driver pcf85063_driver = {
    .name = "pcf85063",
    .compatible = (const char*[]) { "nxp,pcf85063", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &pcf85063_rtc_api,
    .device_type = &RTC_TYPE,
    .internal = nullptr
};

} // extern "C"
