// SPDX-License-Identifier: Apache-2.0
#include <drivers/rx8130ce.h>
#include <rx8130ce_module.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "RX8130CE"

// Register map (RX8130CE datasheet, section 8)
static constexpr uint8_t REG_SECONDS  = 0x10; // BCD seconds
static constexpr uint8_t REG_MINUTES  = 0x11; // BCD minutes
static constexpr uint8_t REG_HOURS    = 0x12; // BCD hours (24h)
static constexpr uint8_t REG_WEEKDAY  = 0x13; // weekday (ignored)
static constexpr uint8_t REG_DAY      = 0x14; // BCD day
static constexpr uint8_t REG_MONTH    = 0x15; // BCD month
static constexpr uint8_t REG_YEAR     = 0x16; // BCD year (00–99, base 2000)
// 0x17–0x1B: alarm / timer registers (not used here)
static constexpr uint8_t REG_FLAG     = 0x1D; // status flags (VLF bit 1)
static constexpr uint8_t REG_CTRL0    = 0x1E; // Control 0 — STOP bit (bit 6)
static constexpr uint8_t REG_CTRL1    = 0x1F; // Control 1 — battery backup bits

static constexpr uint8_t CTRL0_STOP   = 0x40; // bit 6: stop the clock oscillator (RX8130_BIT_CTRL_STOP)
static constexpr uint8_t FLAG_VLF     = 0x02; // bit 1: voltage-low flag (time unreliable)

static constexpr TickType_t I2C_TIMEOUT_TICKS = pdMS_TO_TICKS(50);

#define GET_CONFIG(device) (static_cast<const Rx8130ceConfig*>((device)->config))

// region Helpers

static uint8_t bcd_to_dec(uint8_t bcd) { return static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F)); }
static uint8_t dec_to_bcd(uint8_t dec) { return static_cast<uint8_t>(((dec / 10) << 4) | (dec % 10)); }

// endregion

// region Driver lifecycle

static error_t start(Device* device) {
    auto* i2c_controller = device_get_parent(device);
    if (device_get_type(i2c_controller) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    auto address = GET_CONFIG(device)->address;

    // Clear VLF and other status flags
    if (i2c_controller_register8_set(i2c_controller, address, REG_FLAG, 0x00, I2C_TIMEOUT_TICKS) != ERROR_NONE) {
        LOG_E(TAG, "Failed to clear FLAG register at 0x%02X", address);
        return ERROR_RESOURCE;
    }

    // Enable battery backup: set bits 4 and 5 of CTRL1 (0x1F)
    uint8_t ctrl1 = 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_CTRL1, &ctrl1, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    ctrl1 |= 0x30; // bits 4+5: BSTP=1, BPWD=1
    if (i2c_controller_write_register(i2c_controller, address, REG_CTRL1, &ctrl1, 1, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Ensure oscillator is running (STOP=0)
    uint8_t ctrl0 = 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_CTRL0, &ctrl0, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    if (ctrl0 & CTRL0_STOP) {
        ctrl0 &= static_cast<uint8_t>(~CTRL0_STOP);
        if (i2c_controller_write_register(i2c_controller, address, REG_CTRL0, &ctrl0, 1, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    // RTC oscillator should continue running on battery backup
    // No action needed on driver stop
    return ERROR_NONE;
}

// endregion

extern "C" {

error_t rx8130ce_get_datetime(Device* device, Rx8130ceDateTime* dt) {
    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // Check VLF (voltage-low flag) before reading time data
    uint8_t flag = 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_FLAG, &flag, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    if (flag & FLAG_VLF) {
        LOG_E(TAG, "Clock integrity compromised (VLF flag set) — data unreliable");
        return ERROR_INVALID_STATE;
    }

    // Burst-read 7 registers starting at 0x10:
    // [0]=sec [1]=min [2]=hours [3]=weekday [4]=day [5]=month [6]=year
    uint8_t buf[7] = {};
    error_t error = i2c_controller_read_register(i2c_controller, address, REG_SECONDS, buf, sizeof(buf), I2C_TIMEOUT_TICKS);
    if (error != ERROR_NONE) return error;

    dt->second = bcd_to_dec(buf[0] & 0x7Fu);
    dt->minute = bcd_to_dec(buf[1] & 0x7Fu);
    dt->hour   = bcd_to_dec(buf[2] & 0x3Fu);
    // buf[3] = weekday — ignored
    dt->day    = bcd_to_dec(buf[4] & 0x3Fu);
    dt->month  = bcd_to_dec(buf[5] & 0x1Fu);
    dt->year   = static_cast<uint16_t>(2000 + bcd_to_dec(buf[6]));

    return ERROR_NONE;
}

error_t rx8130ce_set_datetime(Device* device, const Rx8130ceDateTime* dt) {
    if (dt->month < 1 || dt->month > 12 ||
        dt->day < 1 || dt->day > 31 ||
        dt->hour > 23 || dt->minute > 59 || dt->second > 59) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (dt->year < 2000 || dt->year > 2099) {
        return ERROR_INVALID_ARGUMENT;
    }

    auto* i2c_controller = device_get_parent(device);
    auto address = GET_CONFIG(device)->address;

    // Set STOP bit to freeze the oscillator during the write (prevents tearing)
    uint8_t ctrl0 = 0;
    if (i2c_controller_register8_get(i2c_controller, address, REG_CTRL0, &ctrl0, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;
    uint8_t ctrl0_stop = ctrl0 | CTRL0_STOP;
    if (i2c_controller_write_register(i2c_controller, address, REG_CTRL0, &ctrl0_stop, 1, I2C_TIMEOUT_TICKS) != ERROR_NONE) return ERROR_RESOURCE;

    // Write 7 time registers in one burst
    uint8_t buf[7] = {};
    buf[0] = dec_to_bcd(dt->second);
    buf[1] = dec_to_bcd(dt->minute);
    buf[2] = dec_to_bcd(dt->hour);
    buf[3] = 0; // weekday — unused
    buf[4] = dec_to_bcd(dt->day);
    buf[5] = dec_to_bcd(dt->month);
    uint8_t year_offset = static_cast<uint8_t>(dt->year - 2000);
    buf[6] = dec_to_bcd(year_offset);
    error_t error = i2c_controller_write_register(i2c_controller, address, REG_SECONDS, buf, sizeof(buf), I2C_TIMEOUT_TICKS);

    // Clear STOP bit to resume — do this even if the write failed
    ctrl0 &= static_cast<uint8_t>(~CTRL0_STOP);
    error_t resume_error = i2c_controller_write_register(i2c_controller, address, REG_CTRL0, &ctrl0, 1, I2C_TIMEOUT_TICKS);
    if (resume_error != ERROR_NONE) {
        LOG_E(TAG, "Failed to clear STOP bit after datetime write");
        if (error == ERROR_NONE) {
            return resume_error;
        }
    }

    return error;
}

Driver rx8130ce_driver = {
    .name = "rx8130ce",
    .compatible = (const char*[]) { "epson,rx8130ce", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &rx8130ce_module,
    .internal = nullptr
};

} // extern "C"
