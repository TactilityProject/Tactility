#include "tab5_headphone_detect.h"
#include "tab5_speaker_amp.h"

#include <tactility/error.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>
#include <tactility/concurrent/mutex.h>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include <atomic>

constexpr auto* TAG = "Tab5";

// PI4IOE5V6408-0 (0x43) bit 7
constexpr auto GPIO_EXP0_PIN_HEADPHONE_DETECT = 7;

constexpr auto HP_DETECT_POLL_MS = 1000;

static TimerHandle_t hp_detect_timer = nullptr;
// Flags are written by the timer daemon task
static std::atomic hp_detect_last { false };
static std::atomic hp_detect_initialized { false };

// Owns the cached io_expander0 reference.
// Takes care of refcounting and concurrency.
struct HeadphoneDetectCache {
    Mutex mutex {};
    Device* io_expander0 = nullptr;
    bool active = false;

    HeadphoneDetectCache() {
        mutex_construct(&mutex);
    }

    bool isActive() {
        mutex_lock(&mutex);
        bool result = active;
        mutex_unlock(&mutex);
        return result;
    }

    void setActive(bool value) {
        mutex_lock(&mutex);
        active = value;
        mutex_unlock(&mutex);
    }

    Device* getIoExpander0() {
        mutex_lock(&mutex);
        Device* dev = io_expander0;
        if (dev) {
            device_get(dev);
        }
        mutex_unlock(&mutex);
        return dev;
    }

    // Pass nullptr to clear/release the current entry - that always succeeds, regardless of
    // `active`, since it's what stop() uses to tear the cache down.
    bool setIoExpander0(Device* dev) {
        mutex_lock(&mutex);
        if (dev && !active) {
            mutex_unlock(&mutex);
            return false;
        }
        Device* old = io_expander0;
        if (dev) {
            device_get(dev);
        }
        io_expander0 = dev;
        mutex_unlock(&mutex);
        if (old) {
            device_put(old);
        }
        return true;
    }
};

static HeadphoneDetectCache& headphoneDetectCache() {
    static HeadphoneDetectCache instance;
    return instance;
}

static void headphone_detect_callback(TimerHandle_t /*timer*/) {
    auto& cache = headphoneDetectCache();
    if (!cache.isActive()) {
        return; // Teardown is in progress or done - don't acquire/publish a new reference
    }

    Device* io_expander0 = cache.getIoExpander0();
    if (!io_expander0) {
        Device* dev = nullptr;
        if (device_get_by_name("io_expander0", &dev) == ERROR_NONE) {
            if (cache.setIoExpander0(dev)) {
                device_put(dev); // Cache now holds its own reference
                io_expander0 = cache.getIoExpander0();
            } else {
                io_expander0 = dev; // Deactivated concurrently - use our own reference just this once
            }
        }
    }
    if (!io_expander0) {
        return; // Not ready yet, will retry on next tick
    }

    auto* hp_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_HEADPHONE_DETECT, GPIO_FLAG_DIRECTION_INPUT, GPIO_OWNER_GPIO);
    if (!hp_pin) {
        LOG_W(TAG, "hp_detect: HP_DET pin busy");
        device_put(io_expander0);
        return;
    }

    bool hp = false;
    error_t err = gpio_descriptor_get_level(hp_pin, &hp);
    gpio_descriptor_release(hp_pin);

    if (err != ERROR_NONE) {
        LOG_W(TAG, "hp_detect: HP_DET read error: %s", error_to_string(err));
        device_put(io_expander0);
        return;
    }

    LOG_D(TAG, "hp_detect: HP_DET=%d", (int)hp);

    if (!hp_detect_initialized || hp != hp_detect_last) {
        if (!tab5_speaker_amp_set_headphones(hp)) {
            LOG_W(TAG, "hp_detect: speaker amplifier not updated, will retry");
            device_put(io_expander0);
            return;
        }
        hp_detect_last = hp;
        hp_detect_initialized = true;
        LOG_I(TAG, "Headphones %s", hp ? "detected" : "removed");
    }

    device_put(io_expander0);
}

void tab5_headphone_detect_start() {
    if (hp_detect_timer != nullptr) {
        LOG_W(TAG, "hp_detect timer already running");
        return;
    }

    hp_detect_initialized = false;
    hp_detect_last = false;

    auto& cache = headphoneDetectCache();
    cache.setActive(true);

    hp_detect_timer = xTimerCreate("hp_detect", pdMS_TO_TICKS(HP_DETECT_POLL_MS), pdTRUE, nullptr, headphone_detect_callback);
    if (!hp_detect_timer) {
        LOG_E(TAG, "Failed to create hp_detect timer");
        cache.setActive(false);
        return;
    }
    if (xTimerStart(hp_detect_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        LOG_E(TAG, "Failed to start hp_detect timer");
        xTimerDelete(hp_detect_timer, pdMS_TO_TICKS(100));
        hp_detect_timer = nullptr;
        cache.setActive(false);
    }
}

void tab5_headphone_detect_stop() {
    if (hp_detect_timer == nullptr) {
        return;
    }

    auto& cache = headphoneDetectCache();
    // Block any callback invocation from this point on from installing a new reference.
    cache.setActive(false);

    if (xTimerStop(hp_detect_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        LOG_W(TAG, "Failed to stop hp_detect timer");
    }
    if (xTimerDelete(hp_detect_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        LOG_E(TAG, "Failed to delete hp_detect timer");
    }
    // Always clear the handle — stale non-null handle is worse than a resource leak, as it would
    // cause tab5_headphone_detect_start() to silently skip re-creating the timer.
    hp_detect_timer = nullptr;

    cache.setIoExpander0(nullptr);
}
