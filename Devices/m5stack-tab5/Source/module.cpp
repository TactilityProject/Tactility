#include <tactility/module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <cstring>

#include "devices/detect.h"
#include "devices/tab5_headphone_detect.h"
#include "devices/tab5_keyboard.h"
#include "devices/tab5_keyboard_lvgl_rotation.h"
#include "devices/tab5_power_control.h"
#include "devices/tab5_speaker_amp.h"
#include "devices/tab_5_camera.h"

constexpr auto* TAG = "Tab5";

// PI4IOE5V6408-0 (0x43) bit 1
constexpr auto GPIO_EXP0_PIN_SPEAKER_ENABLE = 1;
// PI4IOE5V6408-0 (0x43) bit 7
constexpr auto GPIO_EXP0_PIN_HEADPHONE_DETECT = 7;

static void tab5_init_expander0(Device* io_expander0) {
    // Speaker and heapdhone pins are managed at runtime, so we can't have them as a hog in the dts file
    // We have to init them manually.
    auto* speaker_enable_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_SPEAKER_ENABLE, GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    check(speaker_enable_pin);
    auto* headphone_detect_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_HEADPHONE_DETECT, GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    check(headphone_detect_pin);

    gpio_descriptor_set_level(speaker_enable_pin, false);

    gpio_descriptor_release(speaker_enable_pin);
    gpio_descriptor_release(headphone_detect_pin);
}

// Fires for every device's start/stop in the system. Initializes io_expander0's pins once it has started, and
// connects the speaker amplifier to the codec once that has started.
static void on_device_started(Device* device, DeviceEvent event, void* context) {
    (void)context;

    if (event != DEVICE_EVENT_STARTED) {
        return;
    }
    static bool did_init = false;
    if (!did_init && strcmp(device->name, "io_expander0") == 0) {
        did_init = true;
        tab5_init_expander0(device);
        tab5_speaker_amp_init();
    } else if (strcmp(device->name, "es8388") == 0) {
        tab5_speaker_amp_follow_codec(device);
    }
}

extern "C" {

static error_t start() {
    tab5_detect_start();
    tab5_camera_init();
    device_listener_add(on_device_started, nullptr);
    tab5_headphone_detect_start();
    tab5_keyboard_lvgl_rotation_start();
    return ERROR_NONE;
}

static error_t stop() {
    tab5_keyboard_lvgl_rotation_stop();
    tab5_headphone_detect_stop();
    device_listener_remove(on_device_started, nullptr);
    tab5_detect_stop();
    return ERROR_NONE;
}

static Driver* const tab5_drivers[] = {
    &tab5_keyboard_driver,
    &tab5_power_control_driver,
    nullptr
};

Module m5stack_tab5_module = {
    .name = "m5stack-tab5",
    .start = start,
    .stop = stop,
    .drivers = tab5_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

}
