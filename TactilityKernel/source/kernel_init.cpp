#include <tactility/kernel_init.h>

#include <tactility/device.h>
#include <tactility/log.h>
#include <tactility/log_queue.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TAG "kernel"

extern const ModuleSymbol KERNEL_SYMBOLS[];

extern Driver root_driver;
extern Driver battery_sense_driver;
extern Driver battery_sense_power_supply_driver;
extern Driver gpio_hog_driver;
extern Driver pwm_backlight_driver;
extern Driver gpio_backlight_driver;
extern Driver rgb_led_gpio_driver;
extern Driver rgb_led_pwm_driver;

/**
 * @warning Because the drivers have no owning module, they cannot be unbound.
 * This is fine for now because we never unload the kernel once it's loaded.
 */
static Driver* const KERNEL_DRIVERS[] = {
    &root_driver,
    &battery_sense_driver,
    &battery_sense_power_supply_driver,
    &gpio_hog_driver,
    &pwm_backlight_driver,
    &gpio_backlight_driver,
    &rgb_led_gpio_driver,
    &rgb_led_pwm_driver,
    nullptr,
};

Module kernel_module = {
    .name = "kernel",
    .start = nullptr,
    .stop = nullptr,
    .drivers = KERNEL_DRIVERS,
    .symbols = static_cast<const struct ModuleSymbol*>(KERNEL_SYMBOLS),
    .internal = nullptr,
};

error_t kernel_init(Module* const dts_modules[], const DtsDevice dts_devices[]) {
    log_queue_init();

    LOG_I(TAG, "init");

    if (module_construct_add_start(&kernel_module) != ERROR_NONE) {
        LOG_E(TAG, "root module init failed");
        return ERROR_RESOURCE;
    }

    Module* const* dts_module = dts_modules;
    while (*dts_module != nullptr) {
        if (module_construct_add_start(*dts_module) != ERROR_NONE) {
            LOG_E(TAG, "dts module init failed: %s", (*dts_module)->name);
            return ERROR_RESOURCE;
        }
        dts_module++;
    }

    // Starting is deferred to device_hotplug_poll_once() below so probe() can run first.
    // DISABLED devices are never registered, so the poller never starts them.
    const DtsDevice* dts_device = dts_devices;
    while (dts_device->device != nullptr) {
        if (dts_device->status == DTS_DEVICE_STATUS_OKAY) {
            if (device_construct_add(dts_device->device, dts_device->compatible) != ERROR_NONE) {
                LOG_E(TAG, "kernel_init failed to construct+add device: %s (%s)", dts_device->device->name, dts_device->compatible);
                return ERROR_RESOURCE;
            }
            device_hotplug_register(dts_device->device);
        } else if (dts_device->status == DTS_DEVICE_STATUS_DISABLED) {
            if (device_construct_add(dts_device->device, dts_device->compatible) != ERROR_NONE) {
                LOG_E(TAG, "kernel_init failed to construct+add device: %s (%s)", dts_device->device->name, dts_device->compatible);
                return ERROR_RESOURCE;
            }
        } else {
            check(false, "DTS status not implemented");
        }
        dts_device++;
    }

    // A mandatory (probe == NULL) device's start failure aborts boot, as before.
    if (device_hotplug_poll_once() != ERROR_NONE) {
        LOG_E(TAG, "kernel_init failed to start a mandatory device");
        return ERROR_RESOURCE;
    }
    device_hotplug_start_polling();

    LOG_I(TAG, "init done");
    return ERROR_NONE;
}

#ifdef __cplusplus
}
#endif
