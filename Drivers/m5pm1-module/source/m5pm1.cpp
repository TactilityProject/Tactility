// SPDX-License-Identifier: Apache-2.0
#include <drivers/m5pm1.h>
#include <m5pm1_module.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <new>

constexpr auto* TAG = "M5PM1";

// Rough LiPo discharge curve floor, used together with the configured reference voltage
// (the 100% point) to estimate a charge percentage from the sensed battery voltage.
static constexpr uint16_t POWER_SUPPLY_MIN_MV = 3200;

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
// PM1_G3: Speaker amplifier enable on M5Stack StickS3
static constexpr uint8_t SPEAKER_AMP_BIT = (1U << 3U);
static constexpr uint8_t SPEAKER_AMP_FUNC_MASK = (0x3U << 6U); // GPIO3 function select bits

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

#define GET_CONFIG(device) (static_cast<const M5pm1Config*>((device)->config))

// ---------------------------------------------------------------------------
// Power supply child device
// ---------------------------------------------------------------------------

static int estimate_capacity_from_mv(uint16_t battery_mv, uint32_t reference_mv) {
    if (battery_mv <= POWER_SUPPLY_MIN_MV) return 0;
    if (battery_mv >= reference_mv) return 100;
    return (battery_mv - POWER_SUPPLY_MIN_MV) * 100 / ((int)reference_mv - POWER_SUPPLY_MIN_MV);
}

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_IS_CHARGING ||
        property == POWER_SUPPLY_PROP_VOLTAGE ||
        property == POWER_SUPPLY_PROP_CAPACITY;
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    // device_get_parent() here is the m5pm1 device itself (this child's parent), not the I2C bus.
    auto* parent = device_get_parent(device);

    if (property == POWER_SUPPLY_PROP_IS_CHARGING) {
        bool charging;
        error_t error = m5pm1_is_charging(parent, &charging);
        if (error != ERROR_NONE) return error;
        out_value->int_value = charging ? 1 : 0;
        return ERROR_NONE;
    }

    if (property == POWER_SUPPLY_PROP_VOLTAGE || property == POWER_SUPPLY_PROP_CAPACITY) {
        uint16_t battery_mv;
        error_t error = m5pm1_get_battery_voltage(parent, &battery_mv);
        if (error != ERROR_NONE) return error;
        out_value->int_value = (property == POWER_SUPPLY_PROP_VOLTAGE)
            ? battery_mv
            : estimate_capacity_from_mv(battery_mv, GET_CONFIG(parent)->power_supply_reference_voltage_mv);
        return ERROR_NONE;
    }

    return ERROR_NOT_SUPPORTED;
}

static bool ps_supports_charge_control(Device*) { return false; }
static bool ps_is_allowed_to_charge(Device*) { return false; }
static error_t ps_set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return true; }
static error_t ps_power_off(Device* device) { return m5pm1_shutdown(device_get_parent(device)); }

static constexpr PowerSupplyApi M5PM1_POWER_SUPPLY_API = {
    .supports_property = ps_supports_property,
    .get_property = ps_get_property,
    .supports_charge_control = ps_supports_charge_control,
    .is_allowed_to_charge = ps_is_allowed_to_charge,
    .set_allowed_to_charge = ps_set_allowed_to_charge,
    .supports_quick_charge = ps_supports_quick_charge,
    .is_quick_charge_enabled = ps_is_quick_charge_enabled,
    .set_quick_charge_enabled = ps_set_quick_charge_enabled,
    .supports_power_off = ps_supports_power_off,
    .power_off = ps_power_off,
};

// Registered (driver_construct_add() in module.cpp) so driver_bind() has a valid ->internal,
// but never matched against a devicetree node: m5pm1_driver wires it up directly by pointer.
Driver m5pm1_power_supply_driver = {
    .name = "m5pm1-power-supply",
    .compatible = (const char*[]) { "m5pm1-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &M5PM1_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &m5pm1_module,
    .internal = nullptr
};

struct M5pm1Internal {
    Device* power_supply_device = nullptr;
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "m5pm1-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &m5pm1_power_supply_driver);

    error = device_add(child);
    if (error != ERROR_NONE) {
        device_destruct(child);
        delete child;
        return error;
    }

    error = device_start(child);
    if (error != ERROR_NONE) {
        device_remove(child);
        device_destruct(child);
        delete child;
        return error;
    }

    out_child = child;
    return ERROR_NONE;
}

static void destroy_power_supply_child(Device* child) {
    check(device_stop(child) == ERROR_NONE);
    check(device_remove(child) == ERROR_NONE);
    check(device_destruct(child) == ERROR_NONE);
    delete child;
}

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

    auto* internal = new(std::nothrow) M5pm1Internal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        delete internal;
        return error;
    }

    device_set_driver_data(device, internal);

    if (!awake) {
        LOG_E(TAG, "M5PM1 not responding — LCD power will not be enabled");
        return ERROR_NONE; // non-fatal: don't crash the kernel
    }

    // Disable I2C idle sleep so the PMIC stays reachable on battery power
    if (i2c_controller_register8_set(i2c, addr, REG_I2C_CFG, 0x00, TIMEOUT) != ERROR_NONE) {
        LOG_W(TAG, "Failed to disable I2C sleep (non-fatal)");
    }

    // BOOST_EN → EXT_5V / Grove / Hat power rail always on
    if (i2c_controller_register8_set_bits(i2c, addr, REG_PWR_CFG, PWR_CFG_BOOST_EN, TIMEOUT) == ERROR_NONE) {
        LOG_I(TAG, "EXT_5V boost enabled");
    } else {
        LOG_W(TAG, "Failed to enable EXT_5V boost (non-fatal)");
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

    // PM1_G3 → speaker amp EN, initially LOW (amp off until audio starts)
    bool spk_ok =
        i2c_controller_register8_reset_bits(i2c, addr, REG_GPIO_FUNC0, SPEAKER_AMP_FUNC_MASK, TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_set_bits  (i2c, addr, REG_GPIO_MODE,  SPEAKER_AMP_BIT, TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_reset_bits(i2c, addr, REG_GPIO_DRV,   SPEAKER_AMP_BIT, TIMEOUT) == ERROR_NONE &&
        i2c_controller_register8_reset_bits(i2c, addr, REG_GPIO_OUT,   SPEAKER_AMP_BIT, TIMEOUT) == ERROR_NONE;
    if (spk_ok) {
        LOG_I(TAG, "Speaker amp pin configured");
    } else {
        LOG_W(TAG, "Failed to configure speaker amp pin");
    }

    if (spk_ok && GET_CONFIG(device)->speaker_amp_enable_at_boot) {
        if (i2c_controller_register8_set_bits(i2c, addr, REG_GPIO_OUT, SPEAKER_AMP_BIT, TIMEOUT) == ERROR_NONE) {
            LOG_I(TAG, "Speaker amp enabled at boot");
        } else {
            LOG_W(TAG, "Failed to enable speaker amp at boot");
        }
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<M5pm1Internal*>(device_get_driver_data(device));
    destroy_power_supply_child(internal->power_supply_device);
    device_set_driver_data(device, nullptr);
    delete internal;
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

error_t m5pm1_set_speaker_enable(Device* device, bool enable) {
    Device* i2c = device_get_parent(device);
    const uint8_t addr = GET_CONFIG(device)->address;
    if (enable) {
        return i2c_controller_register8_set_bits(i2c, addr, REG_GPIO_OUT, SPEAKER_AMP_BIT, TIMEOUT);
    } else {
        return i2c_controller_register8_reset_bits(i2c, addr, REG_GPIO_OUT, SPEAKER_AMP_BIT, TIMEOUT);
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
