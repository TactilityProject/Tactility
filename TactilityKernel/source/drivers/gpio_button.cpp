// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/gpio_button.h>

#include <tactility/Memory.hpp>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/timer.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/button.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <atomic>

constexpr auto* TAG = "gpio_button";

#define GET_CONFIG(device) (static_cast<const GpioButtonConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<GpioButtonInternal*>(device_get_driver_data(device)))

// Timer callbacks and pending function calls all run on the timer daemon task, so the state
// below is only touched from one task, except for the atomics.
struct GpioButtonInternal {
    Device* device = nullptr;
    const GpioButtonConfig* config = nullptr;
    GpioDescriptor* descriptor = nullptr;
    Timer* debounce_timer = nullptr;
    Timer* long_press_timer = nullptr;
    std::atomic<bool> pressed { false };
    std::atomic<bool> fence_reached { false };
    // An edge queued a restart of the debounce that did not run yet. Further edges need no extra queue entry.
    std::atomic<bool> restart_pending { false };
    // The device is stopping, so no timer may be armed or handled anymore
    std::atomic<bool> stopping { false };
    bool long_press_fired = false;
    // The button was already held when the device started, so its release is not an action
    bool ignore_release = false;
};

// Guards the driver data against stop() for readers outside of the timer daemon
struct StateMutex {
    Mutex handle {};
    StateMutex() { mutex_construct(&handle); }
    ~StateMutex() { mutex_destruct(&handle); }
};

static StateMutex state_mutex;

static void emit(GpioButtonInternal* internal, uint32_t code, ButtonValue value) {
    button_emit_event(internal->device, ButtonEvent {
        .code = static_cast<uint16_t>(code),
        .value = value,
        .timestamp_ms = static_cast<uint32_t>(get_millis()),
    });
}

static bool has_long_press(const GpioButtonInternal* internal) {
    return internal->config->long_press_code != BUTTON_KEY_NONE;
}

// region Event handling

static void on_long_press_timeout(void* context) {
    auto* internal = static_cast<GpioButtonInternal*>(context);
    if (internal->stopping.load(std::memory_order_acquire) || !internal->pressed.load(std::memory_order_relaxed) || internal->ignore_release) {
        return;
    }
    internal->long_press_fired = true;
    emit(internal, internal->config->long_press_code, BUTTON_VALUE_PRESSED);
}

static void on_press(GpioButtonInternal* internal) {
    internal->long_press_fired = false;
    if (has_long_press(internal)) {
        timer_reset_with_interval(internal->long_press_timer, internal->config->long_press_ms);
    } else {
        emit(internal, internal->config->short_press_code, BUTTON_VALUE_PRESSED);
    }
}

static void on_release(GpioButtonInternal* internal) {
    if (internal->ignore_release) {
        internal->ignore_release = false;
        return;
    }

    if (!has_long_press(internal)) {
        emit(internal, internal->config->short_press_code, BUTTON_VALUE_RELEASED);
    } else if (internal->long_press_fired) {
        emit(internal, internal->config->long_press_code, BUTTON_VALUE_RELEASED);
    } else {
        timer_stop(internal->long_press_timer);
        emit(internal, internal->config->short_press_code, BUTTON_VALUE_PRESSED);
        emit(internal, internal->config->short_press_code, BUTTON_VALUE_RELEASED);
    }
}

static void on_debounce_timeout(void* context) {
    auto* internal = static_cast<GpioButtonInternal*>(context);
    if (internal->stopping.load(std::memory_order_acquire)) {
        return;
    }

    bool level = false;
    if (gpio_descriptor_get_level(internal->descriptor, &level) != ERROR_NONE) {
        return;
    }
    if (level == internal->pressed.load(std::memory_order_relaxed)) {
        return;
    }

    internal->pressed.store(level, std::memory_order_relaxed);
    if (level) {
        on_press(internal);
    } else {
        on_release(internal);
    }
}

static void restart_debounce(void* context, uint32_t) {
    auto* internal = static_cast<GpioButtonInternal*>(context);
    // Cleared first, so an edge during the restart queues another one
    internal->restart_pending.store(false, std::memory_order_release);
    if (internal->stopping.load(std::memory_order_acquire)) {
        return;
    }
    timer_reset_with_interval(internal->debounce_timer, internal->config->debounce_ms);
}

static void mark_fence_reached(void* context, uint32_t) {
    static_cast<GpioButtonInternal*>(context)->fence_reached.store(true, std::memory_order_release);
}

// Runs in interrupt context
static void on_edge(void* context) {
    auto* internal = static_cast<GpioButtonInternal*>(context);
    // One queued restart covers a burst of edges, so the timer queue doesn't fill up and the daemon never waits on it
    if (!internal->restart_pending.exchange(true, std::memory_order_acq_rel)) {
        if (timer_set_pending_callback(nullptr, restart_debounce, internal, 0, 0) != ERROR_NONE) {
            internal->restart_pending.store(false, std::memory_order_release);
        }
    }
}

// endregion

// region Driver lifecycle

static void destroy(GpioButtonInternal* internal) {
    if (internal->debounce_timer != nullptr) {
        timer_free(internal->debounce_timer);
    }
    if (internal->long_press_timer != nullptr) {
        timer_free(internal->long_press_timer);
    }
    tt::deleteObject(internal);
}

static error_t start(Device* device) {
    LOG_I(TAG, "start %s", device->name);
    const auto* config = GET_CONFIG(device);

    auto* internal = tt::newObject<GpioButtonInternal>();
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->device = device;
    internal->config = config;

    internal->debounce_timer = timer_alloc(TIMER_TYPE_ONCE, config->debounce_ms, on_debounce_timeout, internal);
    internal->long_press_timer = timer_alloc(TIMER_TYPE_ONCE, config->long_press_ms, on_long_press_timeout, internal);
    if (internal->debounce_timer == nullptr || internal->long_press_timer == nullptr) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    gpio_flags_t flags = config->pin.flags | GPIO_FLAG_DIRECTION_INPUT;
    flags = GPIO_FLAG_INTERRUPT_TO_OPTIONS(flags, GPIO_INTERRUPT_ANY_EDGE);
    internal->descriptor = gpio_descriptor_acquire(config->pin.gpio_controller, config->pin.pin, flags, GPIO_OWNER_GPIO);
    if (internal->descriptor == nullptr) {
        LOG_E(TAG, "Failed to acquire GPIO");
        destroy(internal);
        return ERROR_RESOURCE;
    }

    bool level = false;
    gpio_descriptor_get_level(internal->descriptor, &level);
    internal->pressed.store(level, std::memory_order_relaxed);
    internal->ignore_release = level;

    error_t error = gpio_descriptor_add_callback(internal->descriptor, on_edge, internal);
    if (error == ERROR_NONE) {
        error = gpio_descriptor_enable_interrupt(internal->descriptor);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable interrupt: %s", error_to_string(error));
        gpio_descriptor_remove_callback(internal->descriptor);
        gpio_descriptor_release(internal->descriptor);
        destroy(internal);
        return error;
    }

    mutex_lock(&state_mutex.handle);
    device_set_driver_data(device, internal);
    mutex_unlock(&state_mutex.handle);
    return ERROR_NONE;
}

/** The timer daemon runs its queue in order. Once the fence ran, everything that was queued before it has run. */
static void wait_for_timer_daemon(GpioButtonInternal* internal) {
    internal->fence_reached.store(false, std::memory_order_release);
    if (timer_set_pending_callback(nullptr, mark_fence_reached, internal, 0, portMAX_DELAY) == ERROR_NONE) {
        while (!internal->fence_reached.load(std::memory_order_acquire)) {
            delay_ticks(1);
        }
    }
}

static error_t stop(Device* device) {
    LOG_I(TAG, "stop %s", device->name);

    // Readers of the driver data finish before the state is released, and don't find it afterwards
    mutex_lock(&state_mutex.handle);
    auto* internal = GET_INTERNAL(device);
    device_set_driver_data(device, nullptr);
    mutex_unlock(&state_mutex.handle);

    // Queued restarts and expiring timers do nothing from here
    internal->stopping.store(true, std::memory_order_release);

    gpio_descriptor_disable_interrupt(internal->descriptor);
    gpio_descriptor_remove_callback(internal->descriptor);

    // Handles the restarts that were queued before the stop, which no longer arm the debounce timer
    wait_for_timer_daemon(internal);

    timer_stop(internal->debounce_timer);
    timer_stop(internal->long_press_timer);

    // The stop commands ran, so no timer callback that references internal can run anymore
    wait_for_timer_daemon(internal);

    gpio_descriptor_release(internal->descriptor);

    if (internal->long_press_fired && internal->pressed.load(std::memory_order_relaxed)) {
        emit(internal, internal->config->long_press_code, BUTTON_VALUE_RELEASED);
    }

    destroy(internal);
    return ERROR_NONE;
}

// endregion

static error_t is_pressed(Device* device, bool* out_pressed) {
    mutex_lock(&state_mutex.handle);
    auto* internal = GET_INTERNAL(device);
    const bool available = internal != nullptr;
    if (available) {
        *out_pressed = internal->pressed.load(std::memory_order_relaxed);
    }
    mutex_unlock(&state_mutex.handle);
    return available ? ERROR_NONE : ERROR_INVALID_STATE;
}

static constexpr ButtonApi GPIO_BUTTON_API = {
    .is_pressed = is_pressed,
};

extern "C" {

Driver gpio_button_driver = {
    .name = "gpio_button",
    .compatible = (const char*[]) { "gpio-button", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &GPIO_BUTTON_API,
    .device_type = &BUTTON_TYPE,
    .internal = nullptr
};

} // extern "C"
