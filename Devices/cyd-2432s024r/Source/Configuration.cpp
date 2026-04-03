#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <Tactility/lvgl/LvglSync.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static bool initBoot() {
    // Set the RGB LED Pins to output and turn them off
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_27, GPIO_MODE_OUTPUT)); // Red
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_12, GPIO_MODE_OUTPUT)); // Green
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_13, GPIO_MODE_OUTPUT)); // Blue

    // 0 on, 1 off
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, 1)); // Red
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_12, 1)); // Green
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_13, 1)); // Blue

    return driver::pwmbacklight::init(LCD_PIN_BACKLIGHT);
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
