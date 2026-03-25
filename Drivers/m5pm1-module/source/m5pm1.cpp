// SPDX-License-Identifier: Apache-2.0
#include <drivers/m5pm1.h>
#include <m5pm1_module.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "M5PM1"

// ---------------------------------------------------------------------------
// Register map
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_DEVICE_ID    = 0x00; ///< R  - Device ID (0x50)
static constexpr uint8_t REG_PWR_SRC      = 0x04; ///< R  - Power source (0=5VIN, 1=5VINOUT, 2=BAT)
static constexpr uint8_t REG_PWR_CFG      = 0x06; ///< RW - [3]=BOOST_EN [2]=LDO_EN [1]=DCDC_EN [0]=CHG_EN
static constexpr uint8_t REG_I2C_CFG      = 0x09; ///< RW - [4]=SPD(400kHz) [3:0]=SLP_TO(0=off)
static constexpr uint8_t REG_WDT_CNT      = 0x0A; ///< RW - Watchdog countdown (0=disabled, 1–255=seconds)
static constexpr uint8_t REG_WDT_KEY      = 0x0B; ///< W  - Write 0xA5 to feed watchdog
static constexpr uint8_t REG_SYS_CMD      = 0x0C; ///< W  - High nibble=0xA; low: 1=shutdown 2=reboot 3=download
static constexpr uint8_t REG_GPIO_MODE    = 0x10; ///< RW - GPIO direction [4:0] (1=output, 0=input)
static constexpr uint8_t REG_GPIO_OUT     = 0x11; ///< RW - GPIO output level [4:0]
static constexpr uint8_t REG_GPIO_IN      = 0x12; ///< R  - GPIO input state [4:0]
static constexpr uint8_t REG_GPIO_DRV     = 0x13; ///< RW - Drive mode [4:0] (0=push-pull, 1=open-drain)
static constexpr uint8_t REG_GPIO_FUNC0   = 0x16; ///< RW - GPIO0–3 function (2 bits each: 00=GPIO)
static constexpr uint8_t REG_VBAT_L       = 0x22; ///< R  - Battery voltage low byte (mV, 16-bit LE)
static constexpr uint8_t REG_VIN_L        = 0x24; ///< R  - VIN voltage low byte (mV, 16-bit LE)
static constexpr uint8_t REG_5VOUT_L      = 0x26; ///< R  - 5V output voltage low byte (mV, 16-bit LE)
static constexpr uint8_t REG_ADC_RES_L    = 0x28; ///< R  - ADC result low byte (mV, 16-bit LE)
static constexpr uint8_t REG_ADC_CTRL     = 0x2A; ///< RW - [3:1]=channel [0]=START
static constexpr uint8_t REG_BTN_STATUS   = 0x48; ///< R  - [7]=BTN_FLAG(auto-clear) [0]=BTN_STATE
static constexpr uint8_t REG_NEO_CFG      = 0x50; ///< RW - [6]=REFRESH [5:0]=LED_CNT
static constexpr uint8_t REG_NEO_DATA     = 0x60; ///< RW - NeoPixel RGB565 data, 2 bytes per LED (max 32)
static constexpr uint8_t REG_RTC_RAM      = 0xA0; ///< RW - 32 bytes of RTC RAM

// PWR_CFG bit masks
static constexpr uint8_t PWR_CFG_CHG_EN   = (1U << 0U);
static constexpr uint8_t PWR_CFG_DCDC_EN  = (1U << 1U);
static constexpr uint8_t PWR_CFG_LDO_EN   = (1U << 2U);
static constexpr uint8_t PWR_CFG_BOOST_EN = (1U << 3U);

// System command values (high nibble must be 0xA)
static constexpr uint8_t SYS_CMD_SHUTDOWN = 0xA1;
static constexpr uint8_t SYS_CMD_REBOOT   = 0xA2;

// ADC channel for temperature
static constexpr uint8_t ADC_CH_TEMP = 6;

// PM1_G2: LCD power enable on M5Stack StickS3
static constexpr uint8_t LCD_POWER_BIT = (1U << 2U);

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

#define GET_CONFIG(device) (static_cast<const M5pm1Config*>((device)->config))

// ---------------------------------------------------------------------------
// Driver lifecycle
// ---------------------------------------------------------------------------

static error_t start(Device* device) {
    Device* i2c = device_get_parent(device);
    if (device_get_type(i2c) != &I2C_CONTROLLER_TYPE) {
        LOG_E(TAG, "Parent is not an I2C controller");
        return ERROR_RESOURCE;
    }

    const uint8_t addr = GET_CONFIG(device)->address;

    // M5PM1 enters I2C sleep after inactivity. The first transaction after sleep
    // is ignored as the chip wakes up. Retry with increasing delays until ACK.
    bool awake = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        uint8_t chip_id = 0;
        if (i2c_controller_register8_get(i2c, addr, REG_DEVICE_ID, &chip_id, TIMEOUT) == ERROR_NONE) {
            LOG_I(TAG, "M5PM1 online (chip_id=0x%02X)", chip_id);
            awake = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20 * (attempt + 1)));
    }

    if (!awake) {
        LOG_E(TAG, "M5PM1 not responding — LCD power will not be enabled");
        return ERROR_NONE; // non-fatal: don't crash the kernel
    }

    // Disable I2C idle sleep so the PMIC stays reachable on battery power
    if (i2c_controller_register8_set(i2c, addr, REG_I2C_CFG, 0x00, TIMEOUT) != ERROR_NONE) {
        LOG_W(TAG, "Failed to disable I2C sleep (non-fatal)");
    }

    // PM1_G2 → LCD power enable (L3B rail on StickS3)
    // Sequence matches M5GFX: clear FUNC0 bit2, set MODE bit2 output, clear DRV bit2 push-pull, set OUT bit2 high
    bool lcd_ok =
        i2c_controller_register8_reset_bits(i2c, addr, REG_GPIO_FUNC0, LCD_POWER_BIT, TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_set_bits  (i2c, addr, REG_GPIO_MODE,  LCD_POWER_BIT, TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_reset_bits(i2c, addr, REG_GPIO_DRV,   LCD_POWER_BIT, TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_set_bits  (i2c, addr, REG_GPIO_OUT,   LCD_POWER_BIT, TIMEOUT) == ERROR_NONE;

    if (lcd_ok) {
        LOG_I(TAG, "LCD power enabled via PM1_G2");
    } else {
        LOG_E(TAG, "Failed to enable LCD power via PM1_G2");
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    return ERROR_NONE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" {

error_t m5pm1_get_battery_voltage(Device* device, uint16_t* mv) {
    return i2c_controller_register16le_get(device_get_parent(device), GET_CONFIG(device)->address, REG_VBAT_L, mv, TIMEOUT);
}

error_t m5pm1_get_vin_voltage(Device* device, uint16_t* mv) {
    return i2c_controller_register16le_get(device_get_parent(device), GET_CONFIG(device)->address, REG_VIN_L, mv, TIMEOUT);
}

error_t m5pm1_get_5vout_voltage(Device* device, uint16_t* mv) {
    return i2c_controller_register16le_get(device_get_parent(device), GET_CONFIG(device)->address, REG_5VOUT_L, mv, TIMEOUT);
}

error_t m5pm1_get_power_source(Device* device, M5pm1PowerSource* source) {
    uint8_t val = 0;
    error_t err = i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_SRC, &val, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *source = static_cast<M5pm1PowerSource>(val & 0x03U);
    return ERROR_NONE;
}

error_t m5pm1_is_charging(Device* device, bool* charging) {
    // PM1_G0 is wired to the charge IC's charge-status output: LOW = charging
    uint8_t gpio_in = 0;
    error_t err = i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, REG_GPIO_IN, &gpio_in, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *charging = (gpio_in & 0x01U) == 0;
    return ERROR_NONE;
}

error_t m5pm1_set_charge_enable(Device* device, bool enable) {
    if (enable) {
        return i2c_controller_register8_set_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_CFG, PWR_CFG_CHG_EN, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_CFG, PWR_CFG_CHG_EN, TIMEOUT);
    }
}

error_t m5pm1_set_boost_enable(Device* device, bool enable) {
    if (enable) {
        return i2c_controller_register8_set_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_CFG, PWR_CFG_BOOST_EN, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_CFG, PWR_CFG_BOOST_EN, TIMEOUT);
    }
}

error_t m5pm1_set_ldo_enable(Device* device, bool enable) {
    if (enable) {
        return i2c_controller_register8_set_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_CFG, PWR_CFG_LDO_EN, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_PWR_CFG, PWR_CFG_LDO_EN, TIMEOUT);
    }
}

error_t m5pm1_get_temperature(Device* device, uint16_t* decidegc) {
    Device* i2c = device_get_parent(device);
    uint8_t addr = GET_CONFIG(device)->address;

    // Select temperature channel and start conversion
    uint8_t ctrl = static_cast<uint8_t>((ADC_CH_TEMP << 1U) | 0x01U);
    error_t err = i2c_controller_register8_set(i2c, addr, REG_ADC_CTRL, ctrl, TIMEOUT);
    if (err != ERROR_NONE) return err;

    // Poll until conversion complete (START bit clears)
    bool conversion_done = false;
    for (int i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t status = 0;
        if (i2c_controller_register8_get(i2c, addr, REG_ADC_CTRL, &status, TIMEOUT) == ERROR_NONE) {
            if ((status & 0x01U) == 0) {
                conversion_done = true;
                break;
            }
        }
    }

    if (!conversion_done) {
        return ERROR_TIMEOUT;
    }

    return i2c_controller_register16le_get(i2c, addr, REG_ADC_RES_L, decidegc, TIMEOUT);
}

error_t m5pm1_shutdown(Device* device) {
    uint8_t cmd = SYS_CMD_SHUTDOWN;
    return i2c_controller_write_register(device_get_parent(device), GET_CONFIG(device)->address, REG_SYS_CMD, &cmd, 1, TIMEOUT);
}

error_t m5pm1_reboot(Device* device) {
    uint8_t cmd = SYS_CMD_REBOOT;
    return i2c_controller_write_register(device_get_parent(device), GET_CONFIG(device)->address, REG_SYS_CMD, &cmd, 1, TIMEOUT);
}

error_t m5pm1_btn_get_state(Device* device, bool* pressed) {
    uint8_t val = 0;
    error_t err = i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, REG_BTN_STATUS, &val, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *pressed = (val & 0x01U) != 0;
    return ERROR_NONE;
}

error_t m5pm1_btn_get_flag(Device* device, bool* was_pressed) {
    uint8_t val = 0;
    error_t err = i2c_controller_register8_get(device_get_parent(device), GET_CONFIG(device)->address, REG_BTN_STATUS, &val, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *was_pressed = (val & 0x80U) != 0; // BTN_FLAG auto-clears on read
    return ERROR_NONE;
}

error_t m5pm1_wdt_set(Device* device, uint8_t timeout_sec) {
    return i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, REG_WDT_CNT, timeout_sec, TIMEOUT);
}

error_t m5pm1_wdt_feed(Device* device) {
    return i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, REG_WDT_KEY, 0xA5, TIMEOUT);
}

error_t m5pm1_read_rtc_ram(Device* device, uint8_t offset, uint8_t* data, uint8_t len) {
    if (offset + len > 32) return ERROR_INVALID_ARGUMENT;
    return i2c_controller_read_register(device_get_parent(device), GET_CONFIG(device)->address, static_cast<uint8_t>(REG_RTC_RAM + offset), data, len, TIMEOUT);
}

error_t m5pm1_write_rtc_ram(Device* device, uint8_t offset, const uint8_t* data, uint8_t len) {
    if (offset + len > 32) return ERROR_INVALID_ARGUMENT;
    return i2c_controller_write_register(device_get_parent(device), GET_CONFIG(device)->address, static_cast<uint8_t>(REG_RTC_RAM + offset), data, len, TIMEOUT);
}

error_t m5pm1_set_led_count(Device* device, uint8_t count) {
    if (count == 0 || count > 32) return ERROR_INVALID_ARGUMENT;
    uint8_t val = count & 0x3FU;
    return i2c_controller_register8_set(device_get_parent(device), GET_CONFIG(device)->address, REG_NEO_CFG, val, TIMEOUT);
}

error_t m5pm1_set_led_color(Device* device, uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= 32) return ERROR_INVALID_ARGUMENT;
    Device* i2c = device_get_parent(device);
    uint8_t addr = GET_CONFIG(device)->address;
    // Store as RGB565: [15:11]=R5, [10:5]=G6, [4:0]=B5
    uint16_t rgb565 = static_cast<uint16_t>(((r >> 3U) << 11U) | ((g >> 2U) << 5U) | (b >> 3U));
    uint8_t buf[2] = { static_cast<uint8_t>(rgb565 & 0xFFU), static_cast<uint8_t>(rgb565 >> 8U) };
    return i2c_controller_write_register(i2c, addr, static_cast<uint8_t>(REG_NEO_DATA + index * 2U), buf, 2, TIMEOUT);
}

error_t m5pm1_refresh_leds(Device* device) {
    return i2c_controller_register8_set_bits(device_get_parent(device), GET_CONFIG(device)->address, REG_NEO_CFG, 0x40U, TIMEOUT);
}

error_t m5pm1_disable_leds(Device* device) {
    // Set count to 1 and write black, then refresh
    Device* i2c = device_get_parent(device);
    uint8_t addr = GET_CONFIG(device)->address;
    uint8_t black[2] = { 0, 0 };
    error_t err = i2c_controller_write_register(i2c, addr, REG_NEO_DATA, black, 2, TIMEOUT);
    if (err != ERROR_NONE) return err;
    uint8_t cfg = 0x41U; // REFRESH | count=1
    return i2c_controller_register8_set(i2c, addr, REG_NEO_CFG, cfg, TIMEOUT);
}

Driver m5pm1_driver = {
    .name = "m5pm1",
    .compatible = (const char*[]) { "m5stack,m5pm1", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &m5pm1_module,
    .internal = nullptr
};

} // extern "C"
