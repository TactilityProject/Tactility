// SPDX-License-Identifier: Apache-2.0
#include <drivers/t5s3_display_bw.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/module.h>

#include "t5s3_epd_board.h"

#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#define TAG "T5s3DisplayBw"
#define GET_CONFIG(device) (static_cast<const T5s3DisplayBwConfig*>((device)->config))

// The panel is driven without waveform tables. A software state machine decides per pixel and
// per frame what to drive, and a task streams the frames. Each pixel is driven toward its target color for
// DRIVE_FRAMES frames after the target changes and is left alone afterwards. Only the bounding box of recent
// changes is processed.

static constexpr int PANEL_WIDTH = T5S3_EPD_WIDTH;
static constexpr int PANEL_HEIGHT = T5S3_EPD_HEIGHT;

// The target is 1 bit per pixel and the state is 4 bits per pixel
static constexpr size_t TARGET_ROW_BYTES = PANEL_WIDTH / 8;
static constexpr size_t STATE_ROW_BYTES = PANEL_WIDTH / 2;
static constexpr size_t TARGET_SIZE = TARGET_ROW_BYTES * PANEL_HEIGHT;
static constexpr size_t STATE_SIZE = STATE_ROW_BYTES * PANEL_HEIGHT;

// Frames a pixel is driven after its target changed
static constexpr int DRIVE_FRAMES = 4;

// Pixel state with both pixels white and fully driven, so nothing is driven
static constexpr uint8_t STATE_SETTLED_WHITE = 0x90;

// Frames are padded to this duration so every drive step has the same length
static constexpr int64_t FRAME_PERIOD_US = 16667;

// Gate pulse of a row in units of 0.1 us
static constexpr uint32_t ROW_HOLD_TICKS = 120;

// Full panel black and white passes that remove ghosting
static constexpr int FLASH_CYCLES = 1;
static constexpr int FLASH_FIELDS = 8;

static constexpr uint32_t IDLE_POWER_OFF_MS = 2000;

static constexpr uint32_t CMD_UPDATE = 1U << 0;
static constexpr uint32_t CMD_FLASH = 1U << 1;
static constexpr uint32_t CMD_POWER_OFF = 1U << 2;
static constexpr uint32_t CMD_EXIT = 1U << 3;

// 4x4 ordered (Bayer) dither thresholds
static constexpr uint8_t BAYER_4X4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

// Thresholds scaled by 16 stay below 255, so pure white is never classified as black
static inline bool dither_is_black(uint8_t luminance, int32_t x, int32_t y) {
    return luminance <= BAYER_4X4[y & 3][x & 3] * 16U;
}

extern "C" {

struct Region {
    int x0, y0, x1, y1;

    bool empty() const { return x1 <= x0 || y1 <= y0; }

    void merge(const Region& other) {
        if (other.empty()) return;
        if (empty()) {
            *this = other;
            return;
        }
        x0 = std::min(x0, other.x0);
        y0 = std::min(y0, other.y0);
        x1 = std::max(x1, other.x1);
        y1 = std::max(y1, other.y1);
    }
};

static constexpr Region EMPTY_REGION = { 0, 0, 0, 0 };
static constexpr Region FULL_REGION = { 0, 0, PANEL_WIDTH, PANEL_HEIGHT };

struct T5s3DisplayBwInternal {
    TaskHandle_t task;
    SemaphoreHandle_t request_mutex;
    SemaphoreHandle_t request_done;
    portMUX_TYPE lock;
    volatile bool exited;
    bool powered;
    // Set by the caller of a flash request, read by the task
    bool flash_wipes_content;
    // Changes not yet picked up by the task, guarded by lock
    Region pending;
    // Time of the first change of the current burst of updates and the longest draw_bitmap() call, guarded by lock
    int64_t burst_start_us;
    int64_t draw_max_us;
    // Columns [x0, x1) that the frame being sent encodes
    int encode_x0;
    int encode_x1;
    // 1 bit per pixel with the first pixel of a byte in the lowest bit, 1 is black
    uint8_t* target;
    uint8_t* state;
};

// region Pixel state machine

// A state byte holds two pixels. The first pixel uses bits 7:5 for its drive counter and bit 1 for its color,
// the second uses bits 4:2 and bit 0. A counter of 4 means fully driven. target_pair holds the 2 target color
// bits of the pixel pair. Returns 4 output bits for 2 pixels, where 01 drives to black and 10 drives to white.
static inline uint8_t IRAM_ATTR drive_pair(uint8_t* state_ptr, uint8_t target_pair) {
    // Indexed by which pixels changed color, keeps the counters of the unchanged ones
    static constexpr uint8_t KEEP_COUNTERS[4] = { 0xFC, 0xE0, 0x1C, 0x00 };

    uint8_t state = *state_ptr;
    state &= KEEP_COUNTERS[(state ^ target_pair) & 0x3];
    state |= target_pair;

    uint8_t out = 0;
    out |= (state & 0x80) ? 0x0 : (target_pair & 0x2) ? 0x4 : 0x8;
    out |= (state & 0x10) ? 0x0 : (target_pair & 0x1) ? 0x1 : 0x2;

    // Increment the counters that have not reached 4
    state += ((~state) >> 2) & 0x24;
    *state_ptr = state;
    return out;
}

// Encodes the columns [encode_x0, encode_x1) of a row, both multiples of 8. Other columns of out stay untouched.
// The panel wants the first pixel in the lowest bits, so the second pixel of a pair goes into the first slot of drive_pair().
static void IRAM_ATTR encode_row(int y, uint8_t* out, void* context) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(context);
    const int x0 = internal->encode_x0;
    const uint8_t* target = internal->target + y * TARGET_ROW_BYTES + x0 / 8;
    uint8_t* state = internal->state + y * STATE_ROW_BYTES + x0 / 2;
    out += x0 / 4;

    for (int count = (internal->encode_x1 - x0) / 8; count > 0; count--) {
        const uint8_t pixels = *target++;
        out[0] = static_cast<uint8_t>(drive_pair(state, pixels & 0x3) | (drive_pair(state + 1, (pixels >> 2) & 0x3) << 4));
        out[1] = static_cast<uint8_t>(drive_pair(state + 2, (pixels >> 4) & 0x3) | (drive_pair(state + 3, pixels >> 6) << 4));
        state += 4;
        out += 2;
    }
}

static void pad_frame(int64_t frame_start_us) {
    const int64_t remaining_us = FRAME_PERIOD_US - (esp_timer_get_time() - frame_start_us);
    if (remaining_us > 2000) {
        vTaskDelay(pdMS_TO_TICKS((remaining_us - 1000) / 1000));
    }
    while (esp_timer_get_time() - frame_start_us < FRAME_PERIOD_US) {}
}

static bool run_update_frame(T5s3DisplayBwInternal* internal, const Region& region) {
    const int64_t start_us = esp_timer_get_time();
    // Columns outside the region must stay zero so they are not driven
    t5s3_epd_board_fill_rows(0);
    internal->encode_x0 = region.x0;
    internal->encode_x1 = region.x1;
    const bool success = t5s3_epd_board_frame(region.y0, region.y1, nullptr, ROW_HOLD_TICKS, encode_row, internal);
    pad_frame(start_us);
    return success;
}

// Drives every pixel toward one color
static bool run_solid_frames(uint8_t pattern, int frames) {
    t5s3_epd_board_fill_rows(pattern);
    bool success = true;
    for (int frame = 0; frame < frames && success; frame++) {
        success = t5s3_epd_board_frame(0, PANEL_HEIGHT, nullptr, ROW_HOLD_TICKS, nullptr, nullptr);
    }
    return success;
}

static bool run_flash() {
    bool success = true;
    for (int cycle = 0; cycle < FLASH_CYCLES && success; cycle++) {
        success = run_solid_frames(T5S3_EPD_ALL_DARK, FLASH_FIELDS) && run_solid_frames(T5S3_EPD_ALL_LIGHT, FLASH_FIELDS);
    }
    return success;
}

static void power_on(T5s3DisplayBwInternal* internal) {
    if (internal->powered) return;
    t5s3_epd_board_power_on();
    internal->powered = true;
}

static void power_off(T5s3DisplayBwInternal* internal) {
    if (!internal->powered) return;
    t5s3_epd_board_power_off();
    internal->powered = false;
}

static void run_flash_request(T5s3DisplayBwInternal* internal) {
    const int64_t start_us = esp_timer_get_time();
    power_on(internal);
    const bool success = run_flash();
    memset(internal->state, STATE_SETTLED_WHITE, STATE_SIZE);
    if (!success) {
        LOG_E(TAG, "Flash failed, a peripheral did not respond");
    }

    const int64_t now_us = esp_timer_get_time();
    portENTER_CRITICAL(&internal->lock);
    if (!internal->flash_wipes_content) {
        internal->pending = FULL_REGION;
        internal->burst_start_us = now_us;
    }
    internal->draw_max_us = 0;
    portEXIT_CRITICAL(&internal->lock);
    LOG_I(TAG, "Flashed in %d ms", static_cast<int>((esp_timer_get_time() - start_us) / 1000));
}

static void display_task(void* arg) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(arg);
    Region active = EMPTY_REGION;
    Region burst = EMPTY_REGION;
    int frames_left = 0;
    int burst_frames = 0;
    int64_t burst_longest_frame_us = 0;

    while (true) {
        uint32_t commands = 0;
        TickType_t timeout = portMAX_DELAY;
        if (frames_left > 0) {
            timeout = 0;
        } else if (internal->powered) {
            timeout = pdMS_TO_TICKS(IDLE_POWER_OFF_MS);
        }
        const bool notified = xTaskNotifyWait(0, UINT32_MAX, &commands, timeout) == pdTRUE;

        if (commands & CMD_EXIT) {
            break;
        }

        if (commands & CMD_POWER_OFF) {
            active = EMPTY_REGION;
            frames_left = 0;
            power_off(internal);
        }

        if (commands & CMD_FLASH) {
            run_flash_request(internal);
            active = EMPTY_REGION;
            burst = EMPTY_REGION;
            frames_left = 0;
            burst_frames = 0;
            burst_longest_frame_us = 0;
            xSemaphoreGive(internal->request_done);
        }

        Region pending;
        portENTER_CRITICAL(&internal->lock);
        pending = internal->pending;
        internal->pending = EMPTY_REGION;
        portEXIT_CRITICAL(&internal->lock);

        if (!pending.empty()) {
            active.merge(pending);
            burst.merge(pending);
            frames_left = DRIVE_FRAMES;
        }

        if (frames_left > 0) {
            power_on(internal);
            // Columns are processed in groups of 8 pixels
            Region aligned = active;
            aligned.x0 &= ~7;
            aligned.x1 = (aligned.x1 + 7) & ~7;
            const int64_t frame_start_us = esp_timer_get_time();
            if (!run_update_frame(internal, aligned)) {
                LOG_E(TAG, "Update failed, a peripheral did not respond");
            }
            // The padding is not part of the work of a frame
            burst_longest_frame_us = std::max(burst_longest_frame_us, esp_timer_get_time() - frame_start_us);
            burst_frames++;
            if (--frames_left == 0) {
                active = EMPTY_REGION;
                int64_t burst_start_us;
                int64_t draw_max_us;
                portENTER_CRITICAL(&internal->lock);
                burst_start_us = internal->burst_start_us;
                draw_max_us = internal->draw_max_us;
                internal->burst_start_us = 0;
                internal->draw_max_us = 0;
                portEXIT_CRITICAL(&internal->lock);
                LOG_D(TAG, "Updated %dx%d at %d,%d in %d ms (%d frames, longest %d ms, draw_bitmap up to %d us)",
                    burst.x1 - burst.x0, burst.y1 - burst.y0, burst.x0, burst.y0,
                    burst_start_us != 0 ? static_cast<int>((esp_timer_get_time() - burst_start_us) / 1000) : -1,
                    burst_frames, static_cast<int>(burst_longest_frame_us / 1000), static_cast<int>(draw_max_us)
                );
                burst = EMPTY_REGION;
                burst_frames = 0;
                burst_longest_frame_us = 0;
            }
        } else if (!notified) {
            power_off(internal);
        }
    }

    power_off(internal);
    internal->exited = true;
    vTaskDelete(nullptr);
}

// endregion

// region Helpers

static void notify_task(T5s3DisplayBwInternal* internal, uint32_t command) {
    xTaskNotify(internal->task, command, eSetBits);
}

static error_t flash_blocking(T5s3DisplayBwInternal* internal, bool wipe_content) {
    xSemaphoreTake(internal->request_mutex, portMAX_DELAY);
    internal->flash_wipes_content = wipe_content;
    notify_task(internal, CMD_FLASH);
    xSemaphoreTake(internal->request_done, portMAX_DELAY);
    xSemaphoreGive(internal->request_mutex);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t t5s3_display_bw_reset(Device* device) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(device_get_driver_data(device));
    notify_task(internal, CMD_POWER_OFF);
    return ERROR_NONE;
}

static error_t t5s3_display_bw_init(Device*) {
    return ERROR_NONE;
}

static error_t t5s3_display_bw_clear(Device* device) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(device_get_driver_data(device));
    memset(internal->target, 0, TARGET_SIZE);
    return flash_blocking(internal, true);
}

static error_t t5s3_display_bw_refresh(Device* device) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(device_get_driver_data(device));
    return flash_blocking(internal, false);
}

// Reports GRAYSCALE8 so LVGL renders partial tiles. The luminance is dithered to black and white.
static error_t t5s3_display_bw_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(device_get_driver_data(device));
    const int64_t start_us = esp_timer_get_time();
    const int32_t width = x_end - x_start;
    const auto* source = static_cast<const uint8_t*>(color_data);

    for (int32_t y = y_start; y < y_end; y++) {
        const uint8_t* source_row = source + (y - y_start) * width;
        uint8_t* target_row = internal->target + y * TARGET_ROW_BYTES;
        for (int32_t x = x_start; x < x_end; x++) {
            const uint8_t mask = 1U << (x & 7);
            if (dither_is_black(source_row[x - x_start], x, y)) {
                target_row[x / 8] |= mask;
            } else {
                target_row[x / 8] &= ~mask;
            }
        }
    }

    const Region changed = { x_start, y_start, x_end, y_end };
    const int64_t draw_us = esp_timer_get_time() - start_us;
    portENTER_CRITICAL(&internal->lock);
    internal->pending.merge(changed);
    if (internal->burst_start_us == 0) {
        internal->burst_start_us = start_us;
    }
    internal->draw_max_us = std::max(internal->draw_max_us, draw_us);
    portEXIT_CRITICAL(&internal->lock);
    notify_task(internal, CMD_UPDATE);
    return ERROR_NONE;
}

static error_t t5s3_display_bw_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(device_get_driver_data(device));
    if (!on_off) {
        notify_task(internal, CMD_POWER_OFF);
    }
    return ERROR_NONE;
}

static DisplayColorFormat t5s3_display_bw_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

static uint16_t t5s3_display_bw_get_resolution_x(Device*) {
    return PANEL_WIDTH;
}

static uint16_t t5s3_display_bw_get_resolution_y(Device*) {
    return PANEL_HEIGHT;
}

// endregion

static constexpr DisplayApi t5s3_display_bw_api = {
    // draw_bitmap() converts into its own target buffer and never DMAs from the given pointer
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
    .reset = t5s3_display_bw_reset,
    .init = t5s3_display_bw_init,
    .draw_bitmap = t5s3_display_bw_draw_bitmap,
    .clear = t5s3_display_bw_clear,
    .refresh = t5s3_display_bw_refresh,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = t5s3_display_bw_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = t5s3_display_bw_get_color_format,
    .get_resolution_x = t5s3_display_bw_get_resolution_x,
    .get_resolution_y = t5s3_display_bw_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

// region Driver lifecycle

static void destroy(T5s3DisplayBwInternal* internal) {
    if (internal->request_mutex) vSemaphoreDelete(internal->request_mutex);
    if (internal->request_done) vSemaphoreDelete(internal->request_done);
    memory_free(internal->target);
    memory_free(internal->state);
    memory_free(internal);
}

static bool allocate_buffers(T5s3DisplayBwInternal* internal) {
    static const MemoryPolicy external_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 16 };
    internal->target = static_cast<uint8_t*>(memory_calloc_with_policy(1, TARGET_SIZE, &external_policy));
    internal->state = static_cast<uint8_t*>(memory_alloc_with_policy(STATE_SIZE, &external_policy));
    if (internal->target == nullptr || internal->state == nullptr) {
        return false;
    }
    return true;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);
    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    auto* internal = static_cast<T5s3DisplayBwInternal*>(memory_calloc_with_policy(1, sizeof(T5s3DisplayBwInternal), &internal_policy));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->lock = portMUX_INITIALIZER_UNLOCKED;
    internal->request_mutex = xSemaphoreCreateMutex();
    internal->request_done = xSemaphoreCreateBinary();

    if (!internal->request_mutex || !internal->request_done || !allocate_buffers(internal)) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    memset(internal->state, STATE_SETTLED_WHITE, STATE_SIZE);

    if (!t5s3_epd_board_init(config->pixel_clock_hz)) {
        LOG_E(TAG, "Failed to set up the pins and peripherals");
        destroy(internal);
        return ERROR_RESOURCE;
    }

    if (xTaskCreatePinnedToCore(display_task, "t5s3_display_bw", 4096, internal, 1, &internal->task, 1) != pdPASS) {
        LOG_E(TAG, "Failed to create display task");
        t5s3_epd_board_deinit();
        destroy(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    // The boot splash leaves residue that a regular update would never clear
    flash_blocking(internal, true);

    LOG_I(TAG, "Started (%dx%d)", PANEL_WIDTH, PANEL_HEIGHT);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<T5s3DisplayBwInternal*>(device_get_driver_data(device));
    notify_task(internal, CMD_EXIT);
    while (!internal->exited) {
        vTaskDelay(1);
    }
    device_set_driver_data(device, nullptr);
    t5s3_epd_board_deinit();
    destroy(internal);
    return ERROR_NONE;
}

// endregion

Driver t5s3_display_bw_driver = {
    .name = "t5s3-display-bw",
    .compatible = (const char*[]) { "lilygo,t5s3-display-bw", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &t5s3_display_bw_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

}
