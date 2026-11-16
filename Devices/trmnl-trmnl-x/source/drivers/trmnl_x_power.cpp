// SPDX-License-Identifier: Apache-2.0
#include "trmnl_x_power.h"
#include "trmnl_x_gauge_profiles.h"

#include <drivers/bq27427.h>
#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/delay.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/power_supply.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <new>

constexpr auto* TAG = "TrmnlXPower";
#define GET_CONFIG(device) (static_cast<const TrmnlXPowerConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<TrmnlXPowerInternal*>(device_get_driver_data(device)))

// Battery detection timing
static constexpr uint32_t BATTERY_CHARGE_MS = 2;
static constexpr uint64_t BATTERY_TIMEOUT_US = 6000;
static constexpr uint64_t BATTERY_ONE_CELL_THRESHOLD_US = 750;
static constexpr int BATTERY_DETECT_MAX_ATTEMPTS = 20;
static constexpr uint32_t BATTERY_DETECT_RETRY_DELAY_MS = 10;

static constexpr uint32_t GAUGE_RESET_PULSE_MS = 10;
static constexpr uint32_t GAUGE_RESET_RECOVERY_MS = 300;
static constexpr uint32_t GAUGE_READY_TIMEOUT_MS = 5000;
static constexpr int GAUGE_SETUP_ATTEMPTS = 2;

extern "C" {

struct TrmnlXPowerInternal {
    Mutex mutex;
    GpioDescriptor* power_good_descriptor = nullptr;
    GpioDescriptor* charge_status_descriptor = nullptr;
    GpioDescriptor* otg_descriptor = nullptr;
    GpioDescriptor* battery_detect_descriptor = nullptr;
    GpioDescriptor* gauge_reset_descriptor = nullptr;
    Device* power_supply_device = nullptr;
    TrmnlXBatteryCount battery_count = TRMNL_X_BATTERY_NONE;

    TrmnlXPowerInternal() { mutex_construct(&mutex); }
    ~TrmnlXPowerInternal() { mutex_destruct(&mutex); }
};

class ScopedLock {
    Mutex* mutex;

public:

    explicit ScopedLock(TrmnlXPowerInternal* internal) : mutex(&internal->mutex) { mutex_lock(mutex); }
    ~ScopedLock() { mutex_unlock(mutex); }
    ScopedLock(const ScopedLock&) = delete;
    ScopedLock& operator=(const ScopedLock&) = delete;
};

// region Public API

error_t trmnl_x_power_is_charging(Device* device, bool* charging) {
    // The pin is declared active-low in the devicetree, so the level is already logical phew
    return gpio_descriptor_get_level(GET_INTERNAL(device)->charge_status_descriptor, charging);
}

error_t trmnl_x_power_is_vbus_good(Device* device, bool* vbus_good) {
    return gpio_descriptor_get_level(GET_INTERNAL(device)->power_good_descriptor, vbus_good);
}

error_t trmnl_x_power_set_otg_enabled(Device* device, bool enabled) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);
    return gpio_descriptor_set_level(internal->otg_descriptor, enabled);
}

error_t trmnl_x_power_is_otg_enabled(Device* device, bool* enabled) {
    return gpio_descriptor_get_level(GET_INTERNAL(device)->otg_descriptor, enabled);
}

error_t trmnl_x_power_get_battery_count(Device* device, TrmnlXBatteryCount* count) {
    if (count == nullptr) return ERROR_INVALID_ARGUMENT;
    *count = GET_INTERNAL(device)->battery_count;
    return ERROR_NONE;
}

error_t trmnl_x_power_reset_gauge(Device* device) {
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    error_t error = gpio_descriptor_set_level(internal->gauge_reset_descriptor, true);
    if (error != ERROR_NONE) return error;
    delay_millis(GAUGE_RESET_PULSE_MS);
    return gpio_descriptor_set_level(internal->gauge_reset_descriptor, false);
}

// endregion

// region Battery detection

static constexpr uint64_t SET_FLAGS_TRANSACTIONS = 4;
static constexpr uint64_t RELEASE_TAIL_TRANSACTIONS = 2;

static error_t measure_battery_once(GpioDescriptor* pin, gpio_flags_t base_flags, TrmnlXBatteryCount* count) {
    // Charge the RC network
    error_t error = gpio_descriptor_set_level(pin, true);
    if (error != ERROR_NONE) return error;
    error = gpio_descriptor_set_flags(pin, base_flags | GPIO_FLAG_DIRECTION_OUTPUT);
    if (error != ERROR_NONE) return error;
    delay_millis(BATTERY_CHARGE_MS);

    // Release the pin and time how long the network takes to pull it low
    const uint64_t release_begin_us = get_micros_since_boot();
    error = gpio_descriptor_set_flags(pin, base_flags | GPIO_FLAG_DIRECTION_INPUT);
    if (error != ERROR_NONE) return error;
    const uint64_t start_us = get_micros_since_boot();
    const uint64_t release_latency_us = (start_us - release_begin_us) * RELEASE_TAIL_TRANSACTIONS / SET_FLAGS_TRANSACTIONS;

    while (true) {
        bool high = false;
        error = gpio_descriptor_get_level(pin, &high);
        if (error != ERROR_NONE) return error;

        const uint64_t elapsed_us = get_micros_since_boot() - start_us + release_latency_us;
        if (!high) {
            *count = elapsed_us > BATTERY_ONE_CELL_THRESHOLD_US ? TRMNL_X_BATTERY_ONE : TRMNL_X_BATTERY_TWO;
            return ERROR_NONE;
        }
        if (elapsed_us >= BATTERY_TIMEOUT_US) {
            *count = TRMNL_X_BATTERY_NONE;
            return ERROR_NONE;
        }
    }
}

error_t trmnl_x_power_detect_battery_count(Device* device, TrmnlXBatteryCount* count) {
    if (count == nullptr) return ERROR_INVALID_ARGUMENT;
    auto* internal = GET_INTERNAL(device);
    ScopedLock lock(internal);

    bool has_previous = false;
    TrmnlXBatteryCount previous = TRMNL_X_BATTERY_NONE;
    for (int attempt = 0; attempt < BATTERY_DETECT_MAX_ATTEMPTS; attempt++) {
        TrmnlXBatteryCount current = TRMNL_X_BATTERY_NONE;
        error_t error = measure_battery_once(internal->battery_detect_descriptor, GET_CONFIG(device)->pin_battery_detect.flags, &current);
        if (error != ERROR_NONE) return error;

        if (has_previous && current == previous) {
            *count = current;
            return ERROR_NONE;
        }
        previous = current;
        has_previous = true;
        delay_millis(BATTERY_DETECT_RETRY_DELAY_MS);
    }

    LOG_E(TAG, "Battery detection did not settle");
    return ERROR_INVALID_STATE;
}

// endregion

// region Fuel gauge setup

static const Bq27427Profile* profile_for(TrmnlXBatteryCount count) {
    return count == TRMNL_X_BATTERY_TWO ? &TRMNL_X_GAUGE_PROFILE_TWO_CELL : &TRMNL_X_GAUGE_PROFILE_ONE_CELL;
}

// Programs the gauge when it does not hold the profile of the detected pack, 
// then checks that it reports some moderately sane values (you can never tell with these power gauges XD)
static error_t setup_gauge_once(Device* gauge, const Bq27427Profile* profile) {
    bool needed = false;
    error_t error = bq27427_profile_needed(gauge, profile, &needed);
    if (error != ERROR_NONE) return error;

    if (needed) {
        error = bq27427_apply_profile(gauge, profile);
        if (error != ERROR_NONE) return error;

        // The gauge keeps ITPOR set until it measures the open-circuit voltage, which needs the battery to rest 
        // Under load that can take longer than we wait, 
        // but the gauge is fine and keeps converging so warn *just in case*
        error = bq27427_wait_ready(gauge, GAUGE_READY_TIMEOUT_MS);
        if (error == ERROR_TIMEOUT) {
            LOG_W(TAG, "Gauge is still initializing, readings are estimates until the battery rests");
        } else if (error != ERROR_NONE) {
            return error;
        }
    }

    Bq27427Status status;
    return bq27427_get_status(gauge, &status);
}

// A gauge that fails is reset once and tried again 
// TODO: (though... this could loop, must test on real hardware)
static void setup_gauge(Device* device, TrmnlXBatteryCount count) {
    Device* gauge = GET_CONFIG(device)->gauge;
    if (count == TRMNL_X_BATTERY_NONE) {
        LOG_I(TAG, "No battery detected, leaving the fuel gauge alone");
        return;
    }
    if (gauge == nullptr || !device_is_ready(gauge)) {
        LOG_E(TAG, "Fuel gauge is not available");
        return;
    }

    const Bq27427Profile* profile = profile_for(count);
    LOG_I(TAG, "Battery pack with %d cell(s)", static_cast<int>(count));

    for (int attempt = 0; attempt < GAUGE_SETUP_ATTEMPTS; attempt++) {
        error_t error = setup_gauge_once(gauge, profile);
        if (error == ERROR_NONE) return;

        LOG_W(TAG, "Fuel gauge setup failed: %s", error_to_string(error));
        if (attempt + 1 < GAUGE_SETUP_ATTEMPTS) {
            if (trmnl_x_power_reset_gauge(device) != ERROR_NONE) {
                LOG_E(TAG, "Failed to reset the fuel gauge");
                return;
            }
            delay_millis(GAUGE_RESET_RECOVERY_MS);
        }
    }
    LOG_E(TAG, "Fuel gauge setup failed, battery readings are not available");
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
    // device_get_parent() here is the trmnl-x-power device itself (this child's parent awww)
    bool charging;
    error_t error = trmnl_x_power_is_charging(device_get_parent(device), &charging);
    if (error != ERROR_NONE) {
        return error;
    }
    out_value->int_value = charging ? 1 : 0;
    return ERROR_NONE;
}

static bool ps_supports_charge_control(Device*) { return false; }
static bool ps_is_allowed_to_charge(Device*) { return false; }
static error_t ps_set_allowed_to_charge(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_quick_charge(Device*) { return false; }
static bool ps_is_quick_charge_enabled(Device*) { return false; }
static error_t ps_set_quick_charge_enabled(Device*, bool) { return ERROR_NOT_SUPPORTED; }
static bool ps_supports_power_off(Device*) { return false; }
static error_t ps_power_off(Device*) { return ERROR_NOT_SUPPORTED; }

static constexpr PowerSupplyApi TRMNL_X_POWER_SUPPLY_API = {
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

Driver trmnl_x_power_supply_driver = {
    .name = "trmnl-x-power-supply",
    .compatible = (const char*[]) {"trmnl-x-power-supply", nullptr},
    .start_device = nullptr,
    .stop_device = nullptr,
    .probe = nullptr,
    .api = &TRMNL_X_POWER_SUPPLY_API,
    .device_type = &POWER_SUPPLY_TYPE,
    .internal = nullptr
};

static error_t create_power_supply_child(Device* parent, Device*& out_child) {
    auto* child = new (std::nothrow) Device {.address = 0, .name = "trmnl-x-power-supply", .config = nullptr, .parent = nullptr, .flags = 0, .internal = nullptr};
    if (child == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = device_construct(child);
    if (error != ERROR_NONE) {
        delete child;
        return error;
    }

    device_set_parent(child, parent);
    device_set_driver(child, &trmnl_x_power_supply_driver);

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

static error_t acquire_input(const GpioPinSpec& pin, GpioDescriptor** out_descriptor) {
    *out_descriptor = gpio_descriptor_acquire(pin.gpio_controller, pin.pin, pin.flags | GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    return (*out_descriptor != nullptr) ? ERROR_NONE : ERROR_RESOURCE;
}

// The IO expander's output register resets to high, so the initial level is written while the
// pin is still an input. This keeps the OTG boost and the gauge reset line from glitching like crazy
static error_t acquire_output(const GpioPinSpec& pin, bool initial_level, GpioDescriptor** out_descriptor) {
    error_t error = acquire_input(pin, out_descriptor);
    if (error != ERROR_NONE) return error;

    error = gpio_descriptor_set_level(*out_descriptor, initial_level);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_set_flags(*out_descriptor, pin.flags | GPIO_FLAG_DIRECTION_OUTPUT);
    }
    if (error != ERROR_NONE) {
        gpio_descriptor_release(*out_descriptor);
        *out_descriptor = nullptr;
    }
    return error;
}

static void release_descriptors(TrmnlXPowerInternal* internal) {
    GpioDescriptor* descriptors[] = {
        internal->power_good_descriptor,
        internal->charge_status_descriptor,
        internal->otg_descriptor,
        internal->battery_detect_descriptor,
        internal->gauge_reset_descriptor
    };
    for (auto* descriptor: descriptors) {
        if (descriptor != nullptr) {
            gpio_descriptor_release(descriptor);
        }
    }
}

static error_t acquire_descriptors(const TrmnlXPowerConfig* config, TrmnlXPowerInternal* internal) {
    if (acquire_input(config->pin_power_good, &internal->power_good_descriptor) != ERROR_NONE ||
        acquire_input(config->pin_charge_status, &internal->charge_status_descriptor) != ERROR_NONE ||
        acquire_input(config->pin_battery_detect, &internal->battery_detect_descriptor) != ERROR_NONE ||
        acquire_output(config->pin_otg, false, &internal->otg_descriptor) != ERROR_NONE ||
        acquire_output(config->pin_gauge_reset, false, &internal->gauge_reset_descriptor) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure the IO expander pins");
        release_descriptors(internal);
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = new (std::nothrow) TrmnlXPowerInternal();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t error = acquire_descriptors(config, internal);
    if (error != ERROR_NONE) {
        delete internal;
        return error;
    }
    device_set_driver_data(device, internal);

    // The cells are counted before anything draws a lot of current, 
    // so the gauge is set up while the battery is calm
    error = trmnl_x_power_detect_battery_count(device, &internal->battery_count);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to count the battery cells: %s", error_to_string(error));
    } else {
        setup_gauge(device, internal->battery_count);
    }

    error = create_power_supply_child(device, internal->power_supply_device);
    if (error != ERROR_NONE) {
        device_set_driver_data(device, nullptr);
        release_descriptors(internal);
        delete internal;
        return error;
    }
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);
    destroy_power_supply_child(internal->power_supply_device);
    // Leave the boost converter off when nothing manages it anymore
    if (gpio_descriptor_set_level(internal->otg_descriptor, false) != ERROR_NONE) {
        LOG_E(TAG, "Failed to switch off the OTG output");
    }
    release_descriptors(internal);
    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

// endregion

Driver trmnl_x_power_driver = {
    .name = "trmnl-x-power",
    .compatible = (const char*[]) {"trmnl,trmnl-x-power", nullptr},
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = nullptr
};
}
