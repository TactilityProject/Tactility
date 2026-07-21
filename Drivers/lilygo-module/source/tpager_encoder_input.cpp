// SPDX-License-Identifier: Apache-2.0
#include <lilygo/drivers/tpager_encoder_input.h>
#include <lilygo/drivers/tpager_encoder.h>

#include <tactility/device.h>
#include <tactility/log.h>

constexpr auto* TAG = "tpager_encoder";

namespace tpager_encoder {

static lv_indev_t* g_indev = nullptr;
static Device* g_device = nullptr;

// Ported from the deprecated HAL's TpagerEncoder::readCallback(). g_raw_total reconstructs the
// old absolute PCNT counter value (the kernel driver's read_delta() consumes/resets on every
// call instead of accumulating forever), so the hysteresis below behaves identically: only a
// run of more than pulses_click raw pulses commits to a detent, and any remainder is discarded
// rather than carried into the next read (matches the original's pulses_prev = pulses jump).
static void read_cb(lv_indev_t*, lv_indev_data_t* data) {
    constexpr int32_t pulses_click = 4;
    static int32_t raw_total = 0;
    static int32_t committed_total = 0;

    constexpr int enter_filter_threshold = 2;
    static int enter_filter = 0;

    data->enc_diff = 0;
    data->state = LV_INDEV_STATE_RELEASED;

    int32_t delta = 0;
    tpager_encoder_read_delta(g_device, &delta);
    raw_total += delta;

    int32_t pulse_diff = raw_total - committed_total;
    if (pulse_diff > pulses_click || pulse_diff < -pulses_click) {
        data->enc_diff = static_cast<int16_t>(pulse_diff / pulses_click);
        committed_total = raw_total;
    }

    bool pressed = false;
    tpager_encoder_get_button_pressed(g_device, &pressed);
    if (pressed && enter_filter < enter_filter_threshold) {
        enter_filter++;
    }
    if (!pressed && enter_filter > 0) {
        enter_filter--;
    }

    if (enter_filter == enter_filter_threshold) {
        data->state = LV_INDEV_STATE_PRESSED;
    }
}

lv_indev_t* init() {
    if (g_indev != nullptr) {
        LOG_W(TAG, "Already initialized");
        return g_indev;
    }

    if (device_get_first_active_by_type(&TPAGER_ENCODER_TYPE, &g_device) != ERROR_NONE) {
        LOG_E(TAG, "tpager_encoder kernel device not found or not started");
        return nullptr;
    }

    g_indev = lv_indev_create();
    if (g_indev == nullptr) {
        LOG_E(TAG, "Failed to register LVGL input device");
        device_put(g_device);
        g_device = nullptr;
        return nullptr;
    }

    lv_indev_set_type(g_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(g_indev, read_cb);
    LOG_I(TAG, "Initialized");

    return g_indev;
}

void deinit() {
    if (g_indev == nullptr) {
        return;
    }

    lv_indev_delete(g_indev);
    g_indev = nullptr;

    device_put(g_device);
    g_device = nullptr;

    LOG_I(TAG, "Deinitialized");
}

}
