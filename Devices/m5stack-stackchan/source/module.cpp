#include <drivers/py32ioexpander.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/device_listener.h>
#include <tactility/module.h>

#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {

// Boot LED pattern (red/green/blue sweep across the 12-LED WS2812C ring).
// Confirms the PY32L020 body IO expander is working.
static void run_led_boot_pattern(Device* py32) {
    static constexpr uint8_t LED_COUNT = 12;
    static constexpr uint8_t COLORS[][3] = {
        { 255,   0,   0 }, // red
        {   0, 255,   0 }, // green
        {   0,   0, 255 }, // blue
    };

    py32_led_set_count(py32, LED_COUNT);
    for (const auto& color : COLORS) {
        for (uint8_t i = 0; i < LED_COUNT; i++) {
            py32_led_set_color(py32, i, color[0], color[1], color[2]);
        }
        py32_led_refresh(py32);
        vTaskDelay(pdMS_TO_TICKS(150));
    }
    py32_led_disable(py32);
}

static void on_device_event(Device* device, DeviceEvent event, void* context) {
    (void)context;
    if (event != DEVICE_EVENT_STARTED) {
        return;
    }
    if (strcmp(device->name, "py32") == 0) {
        run_led_boot_pattern(device);
    }
}

static error_t start() {
    device_listener_add(on_device_event, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    device_listener_remove(on_device_event);
    return ERROR_NONE;
}

Module m5stack_stackchan_module = {
    .name = "m5stack-stackchan",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
