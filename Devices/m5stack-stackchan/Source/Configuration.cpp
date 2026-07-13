#include "devices/Display.h"
#include "devices/Power.h"

#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>
#include <drivers/py32ioexpander.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Tactility/hal/Configuration.h>
#include <Axp2101Power.h>
#include <Axp2101.h>

using namespace tt::hal;

static const auto* TAG = "StackChan";

// ---------------------------------------------------------------------------
// I2C addresses
// ---------------------------------------------------------------------------
static constexpr uint8_t AXP2101_ADDR = 0x34;

// ---------------------------------------------------------------------------
// AW9523B GPIO expander pin map — same wiring as CoreS3.
// AW88298 reset (P0_2) is driven directly by the aw88298-module driver itself
// (see m5stack,stackchan.dts pin-reset property), not from here.
// ---------------------------------------------------------------------------
constexpr auto AW9523B_PIN_TOUCH_RESET = 0;       // P0_0
constexpr auto AW9523B_PIN_BUS_OUT_ENABLE = 1;    // P0_1
constexpr auto AW9523B_PIN_SD_CARD_SWITCH = 4;    // P0_4
constexpr auto AW9523B_PIN_LCD_RESET = 8 + 1;     // P1_1
constexpr auto AW9523B_PIN_BOOST_ENABLE = 8 + 7;  // P1_7 (SY7088)

static bool initGpioExpander(::Device* aw9523b) {
    struct PinInit { uint8_t pin; bool level; };
    static constexpr PinInit pins[] = {
        { AW9523B_PIN_TOUCH_RESET, true },
        { AW9523B_PIN_BUS_OUT_ENABLE, true },
        { AW9523B_PIN_SD_CARD_SWITCH, true },
        { AW9523B_PIN_LCD_RESET, true },
        { AW9523B_PIN_BOOST_ENABLE, true },
    };

    for (const auto& pinInit : pins) {
        auto* descriptor = gpio_descriptor_acquire(aw9523b, pinInit.pin, GPIO_OWNER_GPIO);
        if (descriptor == nullptr) {
            LOG_E(TAG, "AW9523B: Failed to acquire pin %u", pinInit.pin);
            return false;
        }
        error_t error = gpio_descriptor_set_flags(descriptor, GPIO_FLAG_DIRECTION_OUTPUT);
        if (error == ERROR_NONE) {
            error = gpio_descriptor_set_level(descriptor, pinInit.level);
        }
        gpio_descriptor_release(descriptor);
        if (error != ERROR_NONE) {
            LOG_E(TAG, "AW9523B: Failed to configure pin %u", pinInit.pin);
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// AXP2101 power management — same voltage rails as CoreS3
// ---------------------------------------------------------------------------
static bool initPowerControl(::Device* i2c) {
    // Source: https://github.com/m5stack/M5Unified/blob/b8cfec7fed046242da7f7b8024a4e92004a51ff7/src/utility/Power_Class.cpp#L64
    static constexpr uint8_t reg_data[] = {
        0x90U, 0xBFU,       // LDOS ON/OFF control 0 (backlight)
        0x92U, 18U - 5U,    // ALDO1 = 1.8V (AW88298)
        0x93U, 33U - 5U,    // ALDO2 = 3.3V (ES7210)
        0x94U, 33U - 5U,    // ALDO3 = 3.3V (camera)
        0x95U, 33U - 5U,    // ALDO4 = 3.3V (TF card)
        0x27U, 0x00U,       // PowerKey Hold=1sec / PowerOff=4sec
        0x69U, 0x11U,       // CHGLED setting
        0x10U, 0x30U,       // PMU common config
        0x30U, 0x0FU,       // ADC enabled
    };

    if (i2c_controller_write_register_array(i2c, AXP2101_ADDR, reg_data, sizeof(reg_data), pdMS_TO_TICKS(1000)) != ERROR_NONE) {
        LOG_E(TAG, "AXP2101: Failed to set registers");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// initBoot — called after devicetree devices are constructed/started
// ---------------------------------------------------------------------------
static std::shared_ptr<Axp2101> axp2101;

bool initBoot() {
    auto* i2c = device_find_by_name("i2c_internal");
    if (i2c == nullptr) {
        LOG_E(TAG, "i2c_internal not found");
        return false;
    }

    // Boost enable via AXP2101 before GPIO expander init (same as CoreS3)
    if (!initPowerControl(i2c)) {
        LOG_E(TAG, "AXP2101 init failed");
        return false;
    }

    auto* aw9523b = device_find_by_name("aw9523b");
    if (aw9523b == nullptr) {
        LOG_E(TAG, "aw9523b not found");
        return false;
    }

    if (!initGpioExpander(aw9523b)) {
        LOG_E(TAG, "AW9523B init failed");
        return false;
    }

    // Boot LED pattern — confirms PY32IOExpander is working.
    // PY32 pin 0 = servo VM_EN (output), pin 13 = WS2812C data line.
    auto* py32 = device_find_by_name("py32");
    if (py32 != nullptr) {
        static constexpr uint8_t LED_COUNT = 12;
        static constexpr uint8_t COLORS[][3] = {
            { 255,   0,   0 }, // red
            {   0, 255,   0 }, // green
            {   0,   0, 255 }, // blue
        };
        py32_led_set_count(py32, LED_COUNT);
        for (auto& c : COLORS) {
            for (uint8_t i = 0; i < LED_COUNT; i++) {
                py32_led_set_color(py32, i, c[0], c[1], c[2]);
            }
            py32_led_refresh(py32);
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        py32_led_disable(py32);
    } else {
        LOG_W(TAG, "py32 not found — LED boot pattern skipped");
    }

    // Keep Axp2101 C++ wrapper alive for Axp2101Power (backlight + battery)
    axp2101 = std::make_shared<Axp2101>(i2c);
    return true;
}

// ---------------------------------------------------------------------------
// Device list
// ---------------------------------------------------------------------------
static DeviceVector createDevices() {
    return {
        axp2101,
        std::make_shared<Axp2101Power>(axp2101),
        createPower(),
        createDisplay(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
