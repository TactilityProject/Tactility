#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/lvgl/LvglSync.h>

#include <PwmBacklight.h>

static bool initBoot() {
    if (!driver::pwmbacklight::init(LCD_PIN_BACKLIGHT)) {
        return false;
    }

    // Set the RGB LED Pins to output and turn them off
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT)); // Red
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_16, GPIO_MODE_OUTPUT)); // Green
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_17, GPIO_MODE_OUTPUT)); // Blue

    // 0 on, 1 off
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_4, 1)); // Red
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_16, 1)); // Green
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_17, 1)); // Blue

    // This display has a weird glitch with gamma during boot, which results in uneven dark gray colours.
    // Setting gamma curve index to 0 doesn't work at boot for an unknown reason, so we set the curve index to 1:
    tt::kernel::subscribeSystemEvent(tt::kernel::SystemEvent::BootSplash, [](auto) {
        auto display = tt::hal::findFirstDevice<tt::hal::display::DisplayDevice>(tt::hal::Device::Type::Display);
        assert(display != nullptr);
        tt::lvgl::lock(portMAX_DELAY);
        display->setGammaCurve(1U);
        tt::lvgl::unlock();
    });

    return true;
}

static tt::hal::DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const tt::hal::Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
