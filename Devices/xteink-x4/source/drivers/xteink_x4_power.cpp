// SPDX-License-Identifier: Apache-2.0
#include "xteink_x4_power.h"

#include <tactility/check.h>
#include <tactility/concurrent/thread.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_system.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <new>

constexpr auto* TAG = "XteinkX4Power";
#define GET_CONFIG(device) (static_cast<const XteinkX4PowerConfig*>((device)->config))

// How long to wait after dropping the battery latch before concluding the device
// is still alive (e.g. because USB VBUS is keeping the rail up).
static constexpr TickType_t POWER_OFF_WAIT = pdMS_TO_TICKS(1000);

static constexpr uint32_t POWER_BUTTON_POLL_MS = 20;
static constexpr uint32_t POWER_BUTTON_DEBOUNCE_MS = 30;
static constexpr configSTACK_DEPTH_TYPE POWER_BUTTON_THREAD_STACK_SIZE = 4096;

extern "C" {

extern Module xteink_x4_module;

struct XteinkX4PowerInternal {
    GpioDescriptor* usb_detect_descriptor = nullptr;
    GpioDescriptor* power_off_descriptor = nullptr;
    GpioDescriptor* power_button_descriptor = nullptr;
    gpio_num_t power_off_native_pin = GPIO_NUM_NC;
    gpio_num_t power_button_native_pin = GPIO_NUM_NC;
    Device* power_supply_device = nullptr;
    Device* device = nullptr;
    uint32_t power_button_hold_ms = 0;
    uint32_t wake_hold_ms = 0;
    Thread* power_button_thread = nullptr;
    bool stop_requested = false;
};

error_t xteink_x4_power_is_usb_connected(Device* device, bool* connected) {
    auto* internal = static_cast<XteinkX4PowerInternal*>(device_get_driver_data(device));
    return gpio_descriptor_get_level(internal->usb_detect_descriptor, connected);
}

error_t xteink_x4_power_is_power_button_pressed(Device* device, bool* pressed) {
    auto* internal = static_cast<XteinkX4PowerInternal*>(device_get_driver_data(device));
    return gpio_descriptor_get_level(internal->power_button_descriptor, pressed);
}

error_t xteink_x4_power_off(Device* device) {
    LOG_W(TAG, "Power-off requested");
    // Note: callers are responsible for stopping the display (e.g. EPD refresh) before calling
    // this. GPIO13 gates the battery MOSFET; pulling it LOW and holding it powers the MCU off on
    // battery (mirrors the reference firmware's deep-sleep path). On self-latching units the pull
    // re-engages after a button press; the hold guarantees the pin stays LOW through the loss of
    // the digital domain so the latch doesn't float back on.

    auto* internal = static_cast<XteinkX4PowerInternal*>(device_get_driver_data(device));

    gpio_descriptor_set_level(internal->power_off_descriptor, false);
    if (gpio_hold_en(internal->power_off_native_pin) != ESP_OK) {
        LOG_E(TAG, "Failed to hold power-off pin low");
        return ERROR_RESOURCE;
    }
    // Retain the held state across deep sleep so an RTC wake can't re-engage the
    // battery latch before the driver re-asserts it on boot.
    gpio_deep_sleep_hold_en();

    LOG_W(TAG, "Battery latch released. Waiting for power-off...");
    vTaskDelay(POWER_OFF_WAIT);
    LOG_W(TAG, "Device did not power off as expected (USB power present?)");
    return ERROR_NONE;
}

// region Power button (deep-sleep wake + long-press sleep)

/**
 * @brief Waits for the power button to be released, dropping the battery latch and
 * entering deep sleep once it is.
 * @note On battery the latch cut powers the MCU off before the deep sleep completes and
 * the button physically re-engages the latch to boot again. While USB VBUS is present
 * the MCU stays powered and the GPIO wake source below is what actually resumes it.
 */
error_t xteink_x4_power_enter_sleep(Device* device) {
    auto* internal = static_cast<XteinkX4PowerInternal*>(device_get_driver_data(device));

    // Wait for the button to be released so the armed wake source doesn't fire
    // immediately and wake the device right back up.
    bool pressed = true;
    while (pressed) {
        if (gpio_descriptor_get_level(internal->power_button_descriptor, &pressed) != ERROR_NONE) {
            vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
            continue;
        }
        if (pressed) {
            vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
        }
    }

    gpio_descriptor_set_level(internal->power_off_descriptor, false);
    if (gpio_hold_en(internal->power_off_native_pin) != ESP_OK) {
        LOG_E(TAG, "Failed to hold power-off pin low");
        gpio_descriptor_set_level(internal->power_off_descriptor, true);
        return ERROR_RESOURCE;
    }

    if (esp_deep_sleep_enable_gpio_wakeup(1ULL << internal->power_button_native_pin, ESP_GPIO_WAKEUP_GPIO_LOW) != ESP_OK) {
        LOG_E(TAG, "Failed to arm power-button wakeup");
        gpio_hold_dis(internal->power_off_native_pin);
        gpio_descriptor_set_level(internal->power_off_descriptor, true);
        return ERROR_RESOURCE;
    }

    // Keep the latch cut and the power-button pull-up through deep sleep.
    gpio_deep_sleep_hold_en();
    esp_sleep_config_gpio_isolate();

    LOG_W(TAG, "Entering deep sleep");
    esp_deep_sleep_start();

    // Only reached if the deep sleep was aborted.
    gpio_hold_dis(internal->power_off_native_pin);
    gpio_descriptor_set_level(internal->power_off_descriptor, true);
    return ERROR_NONE;
}

// Whether this boot was caused by the power button: a GPIO wake from a USB-powered deep
// sleep, or a battery cold boot (with the latch dropped, the button is the only way to
// re-engage the battery rail). USB-powered cold boots (flash, plug-in) are excluded.
static bool boot_was_power_button_initiated(const XteinkX4PowerInternal* internal) {
    if (esp_reset_reason() == ESP_RST_DEEPSLEEP) {
        return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO;
    }
    if (esp_reset_reason() == ESP_RST_POWERON) {
        bool usb_connected = true;
        return gpio_descriptor_get_level(internal->usb_detect_descriptor, &usb_connected) == ERROR_NONE && !usb_connected;
    }
    return false;
}

// Battery cold boots are verified so a stray tap (e.g. in a bag) doesn't power the device
// on and drain the battery: the button must register as pressed within wake_hold_ms of the
// driver starting, or the device returns to sleep. Deliberate power-on holds (typically a
// second or more) are still down by the time the driver starts. Deep-sleep GPIO wakes are
// always accepted - the button was physically pressed to wake, and a glitch merely boots a
// USB-powered device once.
static void verify_boot_wake(XteinkX4PowerInternal* internal) {
    if (internal->wake_hold_ms == 0 || !boot_was_power_button_initiated(internal)) {
        return;
    }

    const uint32_t start = get_millis();
    do {
        bool pressed = false;
        if (gpio_descriptor_get_level(internal->power_button_descriptor, &pressed) == ERROR_NONE && pressed) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
    } while ((get_millis() - start) < internal->wake_hold_ms);

    LOG_W(TAG, "Power button not held within %lu ms of boot; returning to sleep", static_cast<unsigned long>(internal->wake_hold_ms));
    if (xteink_x4_power_enter_sleep(internal->device) != ERROR_NONE) {
        LOG_E(TAG, "Failed to return to sleep");
    }
}

static int32_t power_button_monitor(void* context) {
    auto* internal = static_cast<XteinkX4PowerInternal*>(context);

    // The press that powered the device on is still held; ignore it and wait for release
    // so it doesn't count towards the long-press sleep trigger.
    while (!internal->stop_requested) {
        bool pressed = false;
        if (gpio_descriptor_get_level(internal->power_button_descriptor, &pressed) != ERROR_NONE) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        if (!pressed) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
    }

    bool debounced_pressed = false;
    uint32_t last_change_time = 0;
    uint32_t press_start_ticks = 0;

    while (!internal->stop_requested) {
        bool raw_pressed = false;
        if (gpio_descriptor_get_level(internal->power_button_descriptor, &raw_pressed) != ERROR_NONE) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        const uint32_t now = get_millis();
        if ((now - last_change_time) >= POWER_BUTTON_DEBOUNCE_MS && raw_pressed != debounced_pressed) {
            last_change_time = now;
            debounced_pressed = raw_pressed;
            if (debounced_pressed) {
                press_start_ticks = xTaskGetTickCount();
            }
        }

        if (debounced_pressed &&
            (xTaskGetTickCount() - press_start_ticks) >= pdMS_TO_TICKS(internal->power_button_hold_ms)) {
            LOG_I(TAG, "Power button held %lu ms, entering sleep", static_cast<unsigned long>(internal->power_button_hold_ms));
            xteink_x4_power_enter_sleep(internal->device);
            // Reached only if the deep sleep was aborted; require a fresh release before
            // re-arming the trigger.
            debounced_pressed = false;
            last_change_time = get_millis();
        }

        vTaskDelay(pdMS_TO_TICKS(POWER_BUTTON_POLL_MS));
    }

    return 0;
}

// endregion

// region Power supply child device

static bool ps_supports_property(Device*, PowerSupplyProperty property) {
    return property == POWER_SUPPLY_PROP_IS_CHARGING;
}

static error_t ps_get_property(Device* device, PowerSupplyProperty property, PowerSupplyPropertyValue* out_value) {
    if (property != POWER_SUPPLY_PROP_IS_CHARGING) {
        return ERROR_NOT_SUPPORTED;
    }
    // The X4's charge IC has no status pin; "charging" is inferred from VBUS presence, matching
    // the reference firmware (see xteink_x4_power_is_usb_connected()).
    bool connected;
    error_t error = xteink_x4_power_is_usb_connected(device_get_parent(device), &connected);
    if (error != ERROR_NONE) {
        return error;
    }
    out_value->int_value = connected ? 1 : 0;
    return ERROR_NONE;
}

static bool ps_supports_charge_control(Device*) { return false; }
static bool ps_is_allowed_to_charge(Device*) { return false; }
static error_t ps_set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return true; }
static error_t ps_power_off(Device* device) { return xteink_x4_power_off(device_get_parent(device)); }

static constexpr PowerSupplyApi XTEINK_X4_POWER_SUPPLY_API = {
    .supports_property = ps_supports_property,
    .get_property = ps_get_property,
    .supports_charge_control = ps_supports_charge_control,
    .is_allowed_to_charge = ps_is_allowed_to_charge,
    .set_allowed_to_charge = ps_set_allowed_to_charge,
    .supports_quick_charge = ps_supports_quick_charge,
    .is_quick_charge_enabled = ps_is_quick_charge_enabled,
    .set_quick_charge_enabled = ps_set_quick_charge_enabled,
    .supports_power_off = ps_supports_power_off,
    .power_off = ps_power_off,
};

// Registered (driver_construct_add() in module.cpp) so driver_bind() has a valid ->internal, but
// never matched against a devicetree node: xteink_x4_power_driver wires it up directly by pointer.
Driver xteink_x4_power_supply_driver = {
    .name = "xteink-x4-power-supply",
    .compatible = (const char*[]) { "xteink-x4-power-supply", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &XTEINK_X4_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .owner = &xteink_x4_module,
    .internal = nullptr
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new(std::nothrow) Device { .address = 0, .name = "xteink-x4-power-supply", .config = nullptr, .parent = nullptr, .internal = nullptr };
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &xteink_x4_power_supply_driver);

    error = device_add(child);
    if (error != ERROR_NONE) {
        device_destruct(child);
        delete child;
        return error;
    }

    error = device_start(child);
    if (error != ERROR_NONE) {
        device_remove(child);
        device_destruct(child);
        delete child;
        return error;
    }

    out_child = child;
    return ERROR_NONE;
}

static void destroy_power_supply_child(Device* child) {
    check(device_stop(child) == ERROR_NONE);
    check(device_remove(child) == ERROR_NONE);
    check(device_destruct(child) == ERROR_NONE);
    delete child;
}

// endregion

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = new(std::nothrow) XteinkX4PowerInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->usb_detect_descriptor = gpio_descriptor_acquire(config->pin_usb_detect.gpio_controller, config->pin_usb_detect.pin, config->pin_usb_detect.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (internal->usb_detect_descriptor == nullptr) {
        LOG_E(TAG, "Failed to configure usb-detect pin");
        delete internal;
        return ERROR_RESOURCE;
    }

    internal->power_off_descriptor = gpio_descriptor_acquire(config->pin_power_off.gpio_controller, config->pin_power_off.pin, config->pin_power_off.flags | GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    if (internal->power_off_descriptor == nullptr) {
        LOG_E(TAG, "Failed to configure power-off pin");
        gpio_descriptor_release(internal->usb_detect_descriptor);
        delete internal;
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_get_native_pin_number(internal->power_off_descriptor, &internal->power_off_native_pin) != ERROR_NONE) {
        LOG_E(TAG, "Power-off pin has no native pin number");
        gpio_descriptor_release(internal->power_off_descriptor);
        gpio_descriptor_release(internal->usb_detect_descriptor);
        delete internal;
        return ERROR_NOT_SUPPORTED;
    }

    internal->power_button_descriptor = gpio_descriptor_acquire(config->pin_power_button.gpio_controller, config->pin_power_button.pin, config->pin_power_button.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (internal->power_button_descriptor == nullptr) {
        LOG_E(TAG, "Failed to configure power-button pin");
        gpio_descriptor_release(internal->power_off_descriptor);
        gpio_descriptor_release(internal->usb_detect_descriptor);
        delete internal;
        return ERROR_RESOURCE;
    }

    if (gpio_descriptor_get_native_pin_number(internal->power_button_descriptor, &internal->power_button_native_pin) != ERROR_NONE) {
        LOG_E(TAG, "Power-button pin has no native pin number");
        gpio_descriptor_release(internal->power_button_descriptor);
        gpio_descriptor_release(internal->power_off_descriptor);
        gpio_descriptor_release(internal->usb_detect_descriptor);
        delete internal;
        return ERROR_NOT_SUPPORTED;
    }

    // A previous power-off held this pin LOW; that state survives a reset, so release it
    // before asserting the latch or the battery rail stays disconnected on non-self-latching
    // units (see the reference firmware's holdPowerRails()).
    gpio_hold_dis(internal->power_off_native_pin);
    gpio_descriptor_set_level(internal->power_off_descriptor, true);

    error_t error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        gpio_descriptor_release(internal->power_button_descriptor);
        gpio_descriptor_release(internal->power_off_descriptor);
        gpio_descriptor_release(internal->usb_detect_descriptor);
        delete internal;
        return error;
    }

    internal->device = device;
    internal->power_button_hold_ms = config->power_button_hold_ms;
    internal->wake_hold_ms = config->wake_hold_ms;
    device_set_driver_data(device, internal);

    // On a battery cold boot a stray tap would otherwise leave the device running on
    // battery until the next power-off; reject it before anything is started.
    verify_boot_wake(internal);

    if (internal->power_button_hold_ms > 0) {
        internal->power_button_thread = thread_alloc_full(
            "x4_power_button",
            POWER_BUTTON_THREAD_STACK_SIZE,
            power_button_monitor,
            internal,
            tskNO_AFFINITY
        );
        if (internal->power_button_thread == nullptr || thread_start(internal->power_button_thread) != ERROR_NONE) {
            LOG_E(TAG, "Failed to start power-button monitor");
            if (internal->power_button_thread != nullptr) {
                thread_free(internal->power_button_thread);
                internal->power_button_thread = nullptr;
            }
            device_set_driver_data(device, nullptr);
            destroy_power_supply_child(internal->power_supply_device);
            gpio_descriptor_release(internal->power_button_descriptor);
            gpio_descriptor_release(internal->power_off_descriptor);
            gpio_descriptor_release(internal->usb_detect_descriptor);
            delete internal;
            return ERROR_RESOURCE;
        }
    }

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<XteinkX4PowerInternal*>(device_get_driver_data(device));
    if (internal->power_button_thread != nullptr) {
        internal->stop_requested = true;
        thread_join(internal->power_button_thread, portMAX_DELAY, POWER_BUTTON_POLL_MS);
        thread_free(internal->power_button_thread);
        internal->power_button_thread = nullptr;
    }
    destroy_power_supply_child(internal->power_supply_device);
    gpio_descriptor_release(internal->power_button_descriptor);
    gpio_descriptor_release(internal->power_off_descriptor);
    gpio_descriptor_release(internal->usb_detect_descriptor);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

// endregion

Driver xteink_x4_power_driver = {
    .name = "xteink-x4-power",
    .compatible = (const char*[]) { "xteink,x4-power", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &xteink_x4_module,
    .internal = nullptr
};

}
