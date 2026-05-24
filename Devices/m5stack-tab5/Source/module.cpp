#include <tactility/module.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

#include <atomic>

#define TAG "Tab5"

constexpr auto GPIO_EXP0_PIN_SPEAKER_ENABLE = 1;
constexpr auto GPIO_EXP0_PIN_HEADPHONE_DETECT = 7;
constexpr auto HP_DETECT_POLL_MS = 1000;

// hp_detect_timer is only touched from start()/stop(), which are called serially
// by the module manager — no atomic needed for the handle itself.
static TimerHandle_t hp_detect_timer = nullptr;
static std::atomic<Device*> io_expander0_cached { nullptr };
// Flags are written by the timer daemon task and read by start()/stop() — use atomics.
static std::atomic<bool> hp_detect_last { false };
static std::atomic<bool> hp_detect_initialized { false };

static void headphoneDetectCallback(TimerHandle_t /*timer*/) {
    Device* cached = io_expander0_cached.load(std::memory_order_acquire);
    if (!cached) {
        cached = device_find_by_name("io_expander0");
        io_expander0_cached.store(cached, std::memory_order_release);
    }
    auto* io_expander0 = cached;
    if (!io_expander0) {
        return; // Not ready yet, will retry on next tick
    }

    auto* hp_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_HEADPHONE_DETECT, GPIO_OWNER_GPIO);
    if (!hp_pin) {
        LOG_W(TAG, "hp_detect: HP_DET pin busy");
        return;
    }

    bool hp = false;
    error_t err = gpio_descriptor_get_level(hp_pin, &hp);
    gpio_descriptor_release(hp_pin);

    if (err != ERROR_NONE) {
        LOG_W(TAG, "hp_detect: HP_DET read error: %s", error_to_string(err));
        return;
    }

    LOG_D(TAG, "hp_detect: HP_DET=%d", (int)hp);

    if (!hp_detect_initialized || hp != hp_detect_last) {
        auto* spk_pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_SPEAKER_ENABLE, GPIO_OWNER_GPIO);
        if (!spk_pin) {
            LOG_W(TAG, "hp_detect: SPK_EN pin busy, will retry");
            return;
        }
        error_t spk_err = gpio_descriptor_set_level(spk_pin, !hp);
        gpio_descriptor_release(spk_pin);
        if (spk_err != ERROR_NONE) {
            LOG_W(TAG, "hp_detect: SPK_EN set error: %s, will retry", error_to_string(spk_err));
            return;
        }
        hp_detect_last = hp;
        hp_detect_initialized = true;
        LOG_I(TAG, "Headphones %s, speaker %s", hp ? "detected" : "removed", hp ? "disabled" : "enabled");
    }
}

extern "C" {

static error_t start() {

    if (hp_detect_timer != nullptr) {
        LOG_W(TAG, "hp_detect timer already running");
        return ERROR_NONE;
    }

    hp_detect_initialized = false;
    hp_detect_last = false;

    hp_detect_timer = xTimerCreate("hp_detect", pdMS_TO_TICKS(HP_DETECT_POLL_MS), pdTRUE, nullptr, headphoneDetectCallback);
    if (!hp_detect_timer) {
        LOG_E(TAG, "Failed to create hp_detect timer");
        return ERROR_RESOURCE;
    }
    if (xTimerStart(hp_detect_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        LOG_E(TAG, "Failed to start hp_detect timer");
        xTimerDelete(hp_detect_timer, pdMS_TO_TICKS(100));
        hp_detect_timer = nullptr;
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t stop() {
    if (hp_detect_timer == nullptr) {
        return ERROR_NONE;
    }
    if (xTimerStop(hp_detect_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        LOG_W(TAG, "Failed to stop hp_detect timer");
    }
    if (xTimerDelete(hp_detect_timer, pdMS_TO_TICKS(100)) != pdPASS) {
        LOG_E(TAG, "Failed to delete hp_detect timer");
    }
    // Always clear the handle — stale non-null handle is worse than a resource leak,
    // as it would cause start() to silently skip re-creating the timer.
    hp_detect_timer = nullptr;
    io_expander0_cached.store(nullptr, std::memory_order_release);
    return ERROR_NONE;
}

Module m5stack_tab5_module = {
    .name = "m5stack-tab5",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
