#include "tab5_speaker_amp.h"

#include <drivers/es8388.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/log.h>

constexpr auto* TAG = "Tab5";

// PI4IOE5V6408-0 (0x43) bit 1
constexpr auto GPIO_EXP0_PIN_SPEAKER_ENABLE = 1;

namespace {

struct AmpState {
    Mutex mutex {};
    bool codec_active = false;
    bool headphones = false;
    /** The pin doesn't have the wanted level, because a write failed */
    bool dirty = false;

    AmpState() { mutex_construct(&mutex); }
};

AmpState& state() {
    static AmpState instance;
    return instance;
}

/** Requires the state's mutex. A failed write leaves the state dirty, so it can be retried. */
bool apply(AmpState& amp) {
    Device* io_expander0 = nullptr;
    if (device_get_by_name("io_expander0", &io_expander0) != ERROR_NONE) {
        amp.dirty = true;
        return false;
    }
    auto* pin = gpio_descriptor_acquire(io_expander0, GPIO_EXP0_PIN_SPEAKER_ENABLE, GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    bool success = false;
    if (pin != nullptr) {
        const error_t error = gpio_descriptor_set_level(pin, amp.codec_active && !amp.headphones);
        gpio_descriptor_release(pin);
        success = error == ERROR_NONE;
        if (!success) {
            LOG_W(TAG, "Failed to set the speaker amplifier: %s", error_to_string(error));
        }
    }
    device_put(io_expander0);
    amp.dirty = !success;
    return success;
}

void on_codec_output(bool active, void* /*user_data*/) {
    auto& amp = state();
    mutex_lock(&amp.mutex);
    amp.codec_active = active;
    apply(amp);
    mutex_unlock(&amp.mutex);
}

}

void tab5_speaker_amp_init() {
    auto& amp = state();
    mutex_lock(&amp.mutex);
    amp.codec_active = false;
    apply(amp);
    mutex_unlock(&amp.mutex);
}

void tab5_speaker_amp_follow_codec(Device* codec) {
    es838x_set_output_callback(codec, on_codec_output, nullptr);
}

bool tab5_speaker_amp_is_dirty() {
    auto& amp = state();
    mutex_lock(&amp.mutex);
    const bool dirty = amp.dirty;
    mutex_unlock(&amp.mutex);
    return dirty;
}

bool tab5_speaker_amp_set_headphones(bool plugged_in) {
    auto& amp = state();
    mutex_lock(&amp.mutex);
    amp.headphones = plugged_in;
    const bool success = apply(amp);
    mutex_unlock(&amp.mutex);
    return success;
}
