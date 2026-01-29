#include <PwmBacklight.h>
#include "devices/Display.h"

#include <Tactility/hal/Configuration.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace tt::hal;

static bool initBoot() {
    //Display Reset
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_46, GPIO_MODE_OUTPUT));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_46, 0));
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_46, 1));
    vTaskDelay(pdMS_TO_TICKS(10));

    return driver::pwmbacklight::init(GPIO_NUM_21);
}

static DeviceVector createDevices() {
    return {
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
