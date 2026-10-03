#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <tactility/device.h>
#include <tactility/drivers/led_strip.h>
#include <tactility/system_event.h>
#include <Tactility/settings/LedStripSettings.h>

constexpr auto* TAG = "tembed-cc1101-plus";

extern "C" {

static void on_boot_completed(SystemEvent* event, void* context) {
    (void)event;
    (void)context;
    Device* device = nullptr;
    if (device_get_first_by_type(&LED_STRIP_TYPE, &device) != ERROR_NONE) {
        LOG_W(TAG, "No LED strip found during boot restore");
        return;
    }

    const auto settings = tt::settings::ledstrip::loadOrGetDefault(device->name);
    const error_t result = tt::settings::ledstrip::apply(device, settings);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "Failed to restore LED strip %s: %d", device->name, result);
    } else {
        LOG_I(TAG, "Restored LED strip %s", device->name);
    }
    device_put(device);
}

static error_t start() {
    return system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed, nullptr);
}

static error_t stop() {
    return system_event_callback_remove(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed);
}

Module lilygo_tembed_cc1101_plus_module = {
    .name = "lilygo-tembed-cc1101-plus",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
