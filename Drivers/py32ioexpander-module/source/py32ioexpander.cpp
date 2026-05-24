// SPDX-License-Identifier: Apache-2.0
// Reference: https://github.com/m5stack/StackChan/tree/main/firmware/main/hal/drivers/PY32IOExpander_Class
#include <drivers/py32ioexpander.h>
#include <py32ioexpander_module.h>
#include <tactility/device.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>

#define TAG "PY32IOExpander"

// ---------------------------------------------------------------------------
// Register map
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_UID_L = 0x00; ///< R  - Unique ID low byte
static constexpr uint8_t REG_VERSION = 0x02; ///< R  - Firmware version
static constexpr uint8_t REG_GPIO_MODE_L = 0x03; ///< RW - GPIO direction [15:0] (1=output, 0=input), little-endian
static constexpr uint8_t REG_GPIO_OUT_L = 0x05; ///< RW - GPIO output level [15:0], little-endian
static constexpr uint8_t REG_GPIO_IN_L = 0x07; ///< R  - GPIO input state [15:0], little-endian
static constexpr uint8_t REG_LED_CFG = 0x24; ///< RW - [6]=REFRESH [5:0]=LED_COUNT
static constexpr uint8_t REG_LED_DATA = 0x30; ///< RW - RGB565 data, 2 bytes per LED (max 32)

static constexpr TickType_t TIMEOUT = pdMS_TO_TICKS(50);

#define GET_CONFIG(device) (static_cast<const Py32IoExpanderConfig*>((device)->config))

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

    // PY32 may need a moment after power-on. Retry with increasing delays.
    uint8_t version = 0;
    bool online = false;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (i2c_controller_register8_get(i2c, addr, REG_VERSION, &version, TIMEOUT) == ERROR_NONE &&
            version != 0x00 && version != 0xFF) {
            online = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20 * (attempt + 1)));
    }

    if (!online) {
        LOG_E(TAG, "PY32IOExpander not responding at 0x%02X — LED ring will not work", addr);
        return ERROR_NONE; // non-fatal: don't crash the kernel
    }

    LOG_I(TAG, "PY32IOExpander online (addr=0x%02X, fw=0x%02X)", addr, version);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    return ERROR_NONE;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" {

error_t py32_gpio_set_output(Device* device, uint8_t pin, bool value) {
    if (pin > 15U) return ERROR_RESOURCE;
    Device* i2c = device_get_parent(device);
    const uint8_t addr = GET_CONFIG(device)->address;

    // Ensure pin is in output mode
    uint16_t mode = 0;
    error_t err = i2c_controller_register16le_get(i2c, addr, REG_GPIO_MODE_L, &mode, TIMEOUT);
    if (err != ERROR_NONE) return err;
    mode |= static_cast<uint16_t>(1U << pin);
    err = i2c_controller_register16le_set(i2c, addr, REG_GPIO_MODE_L, mode, TIMEOUT);
    if (err != ERROR_NONE) return err;

    // Set output level
    uint16_t out = 0;
    err = i2c_controller_register16le_get(i2c, addr, REG_GPIO_OUT_L, &out, TIMEOUT);
    if (err != ERROR_NONE) return err;
    if (value) {
        out |= static_cast<uint16_t>(1U << pin);
    } else {
        out &= static_cast<uint16_t>(~(1U << pin));
    }
    return i2c_controller_register16le_set(i2c, addr, REG_GPIO_OUT_L, out, TIMEOUT);
}

error_t py32_gpio_get_input(Device* device, uint8_t pin, bool* value) {
    if (pin > 15U) return ERROR_RESOURCE;
    Device* i2c = device_get_parent(device);
    uint16_t in = 0;
    error_t err = i2c_controller_register16le_get(i2c, GET_CONFIG(device)->address, REG_GPIO_IN_L, &in, TIMEOUT);
    if (err != ERROR_NONE) return err;
    *value = (in >> pin) & 1U;
    return ERROR_NONE;
}

error_t py32_led_set_count(Device* device, uint8_t count) {
    if (count > 32U) return ERROR_RESOURCE;
    Device* i2c = device_get_parent(device);
    uint8_t cfg = count & 0x3FU;
    return i2c_controller_register8_set(i2c, GET_CONFIG(device)->address, REG_LED_CFG, cfg, TIMEOUT);
}

error_t py32_led_set_color(Device* device, uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (count > 32U) return ERROR_RESOURCE;
    Device* i2c = device_get_parent(device);
    // RGB565: [15:11]=R5 [10:5]=G6 [4:0]=B5, stored little-endian
    uint16_t rgb565 = static_cast<uint16_t>(((r >> 3U) << 11U) | ((g >> 2U) << 5U) | (b >> 3U));
    uint8_t buf[2] = {static_cast<uint8_t>(rgb565 & 0xFFU), static_cast<uint8_t>(rgb565 >> 8U)};
    return i2c_controller_write_register(i2c, GET_CONFIG(device)->address, static_cast<uint8_t>(REG_LED_DATA + index * 2U), buf, 2, TIMEOUT);
}

error_t py32_led_refresh(Device* device) {
    Device* i2c = device_get_parent(device);
    const uint8_t addr = GET_CONFIG(device)->address;
    // Read current LED_CFG (contains LED count), then set the REFRESH bit (6)
    uint8_t cfg = 0;
    error_t err = i2c_controller_register8_get(i2c, addr, REG_LED_CFG, &cfg, TIMEOUT);
    if (err != ERROR_NONE) return err;
    return i2c_controller_register8_set(i2c, addr, REG_LED_CFG, cfg | 0x40U, TIMEOUT);
}

error_t py32_led_disable(Device* device) {
    // Write black to all 32 possible LED slots then refresh with count=32
    // Using the maximum count ensures no stale color data is displayed.
    static constexpr uint8_t BLACK[64] = {}; // 32 LEDs × 2 bytes, all zero
    Device* i2c = device_get_parent(device);
    uint8_t addr = GET_CONFIG(device)->address;
    error_t err = i2c_controller_write_register(i2c, addr, REG_LED_DATA, BLACK, sizeof(BLACK), TIMEOUT);
    if (err != ERROR_NONE) return err;
    // count=32 | REFRESH
    return i2c_controller_register8_set(i2c, addr, REG_LED_CFG, 0x60U, TIMEOUT);
}

Driver py32ioexpander_driver = {
    .name = "py32ioexpander",
    .compatible = (const char*[]) {"m5stack,py32ioexpander", nullptr},
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &py32ioexpander_module,
    .internal = nullptr
};

} // extern "C"
