#include "tab_5_camera.h"

#include <tactility/log.h>

#include <esp_clock_output.h>

#define TAG "Tab5"

static esp_clock_output_mapping_handle_t camera_osc_handle = nullptr;

// 24 MHz clock on GPIO 36 for the SC2356 MIPI CSI sensor, required before esp_video_init. Uses
// SPLL (480 MHz) via esp_clock_output with divider 20 = 24 MHz exactly. This avoids any LEDC
// clock source conflict with the display backlight.
void tab5_camera_init() {
    if (camera_osc_handle != nullptr) {
        return;
    }

    if (esp_clock_output_start(CLKOUT_SIG_SPLL, GPIO_NUM_36, &camera_osc_handle) != ESP_OK) {
        LOG_E(TAG, "Camera OSC clock output start failed");
        return;
    }
    if (esp_clock_output_set_divider(camera_osc_handle, 20) != ESP_OK) {
        LOG_E(TAG, "Camera OSC clock divider set failed");
        esp_clock_output_stop(camera_osc_handle);
        camera_osc_handle = nullptr;
        return;
    }
    LOG_I(TAG, "Camera OSC 24MHz started on GPIO 36 (SPLL/20)");
}
