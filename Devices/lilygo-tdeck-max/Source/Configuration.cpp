#include "devices/Display.h"
#include "devices/TdeckmaxKeyboard.h"

#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>

using namespace tt::hal;

// LoRa and SD card share the EPD's SPI bus but aren't wired up yet, so their
// chip-select lines are left floating. Deassert them before the EPD driver
// touches the bus, matching the vendor reference driver's own setup(), so a
// floating CS can't make either chip latch EPD command bytes meant for it.
constexpr auto LORA_PIN_CS = GPIO_NUM_3;
constexpr auto SD_PIN_CS = GPIO_NUM_48;

// Reset lines routed through the XL9555 IO expander (P-numbers from the vendor
// lib/TDeckMaxBoard/src/TDeckMaxBoard.h). Both are active low.
constexpr auto XL9555_PIN_TOUCH_RST = 7; // P07
constexpr auto XL9555_PIN_KEY_RST = 9;   // P11

// Release the touch and keyboard reset lines held by the XL9555. Without this
// the touch controller may stay in a half-powered state and the keyboard's
// TCA8418 is held in reset, so neither responds. Mirrors the vendor factory
// firmware's XL9555 init (examples/factory/factory.ino).
static void initIoExpander() {
    auto* xl9555 = device_find_by_name("xl9555");
    if (xl9555 == nullptr) {
        // Boot anyway; display works without the expander, but touch/keyboard won't.
        return;
    }

    auto* touch_rst = gpio_descriptor_acquire(xl9555, XL9555_PIN_TOUCH_RST, GPIO_OWNER_GPIO);
    auto* key_rst = gpio_descriptor_acquire(xl9555, XL9555_PIN_KEY_RST, GPIO_OWNER_GPIO);

    if (touch_rst != nullptr) {
        gpio_descriptor_set_flags(touch_rst, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(touch_rst, false);
    }
    if (key_rst != nullptr) {
        gpio_descriptor_set_flags(key_rst, GPIO_FLAG_DIRECTION_OUTPUT);
        gpio_descriptor_set_level(key_rst, false);
    }

    delay_millis(20);

    // Release both resets and give the controllers time to boot before they're probed.
    if (touch_rst != nullptr) {
        gpio_descriptor_set_level(touch_rst, true);
        gpio_descriptor_release(touch_rst);
    }
    if (key_rst != nullptr) {
        gpio_descriptor_set_level(key_rst, true);
        gpio_descriptor_release(key_rst);
    }

    delay_millis(60);
}

static bool initBoot() {
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << LORA_PIN_CS) | (1ULL << SD_PIN_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
    gpio_set_level(LORA_PIN_CS, 1);
    gpio_set_level(SD_PIN_CS, 1);

    initIoExpander();

    return true;
}

static DeviceVector createDevices() {
    auto* i2c = device_find_by_name("i2c0");

    // SD card is not created here: it's an espressif,esp32-sdspi node in the
    // devicetree (sdcard@1 under spi0), started by Hal::init's mountAll().
    DeviceVector devices = {
        createDisplay()
    };

    if (i2c != nullptr) {
        auto keypad = std::make_shared<Tca8418>(i2c);
        devices.push_back(keypad);
        devices.push_back(std::make_shared<TdeckmaxKeyboard>(keypad));
    }

    return devices;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
