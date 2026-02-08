#include "devices/Display.h"
#include "devices/Power.h"
#include "devices/Constants.h"

#include <tactility/log.h>
#include <tactility/freertos/task.h>

#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>

#include <driver/gpio.h>

static void enableOledPower() {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << DISPLAY_PIN_POWER),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, // The board has an external pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(DISPLAY_PIN_POWER, 0); // Active low

    vTaskDelay(pdMS_TO_TICKS(500)); // Add a small delay for power to stabilize
    LOG_I("HeltecV3", "OLED power enabled");
}

static bool initBoot() {
    // Enable power to the OLED before doing anything else
    enableOledPower();

    return true;
}

using namespace tt::hal;

static std::vector<std::shared_ptr<tt::hal::Device>> createDevices() {
    return {
        createPower(),
        ButtonControl::createOneButtonControl(0),
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .uiScale = UiScale::Smallest,
    .createDevices = createDevices
};
