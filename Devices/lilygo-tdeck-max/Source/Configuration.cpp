#include "devices/Display.h"
#include "devices/TdeckmaxKeyboard.h"

#include <Tactility/hal/Configuration.h>
#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio_controller.h>

using namespace tt::hal;

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
    // The LoRa and SD chip-selects share the EPD's SPI bus but aren't wired up
    // yet. spi0's start() already drives every cs-gpio high during kernel init;
    // re-assert deselection before the EPD driver first transacts, as a cheap
    // guard against either chip latching stray EPD command bytes (same pattern
    // as the esp32_sdspi mount path).
    auto* spi0 = device_find_by_name("spi0");
    check(spi0 != nullptr);
    esp32_spi_deselect_all_cs(spi0);

    initIoExpander();

    return true;
}

static DeviceVector createDevices() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c != nullptr);

    // SD card is not created here: it's an espressif,esp32-sdspi node in the
    // devicetree (sdcard@1 under spi0), started by Hal::init's mountAll().
    DeviceVector devices = {
        createDisplay()
    };

    auto keypad = std::make_shared<Tca8418>(i2c);
    devices.push_back(keypad);
    devices.push_back(std::make_shared<TdeckmaxKeyboard>(keypad));

    return devices;
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
