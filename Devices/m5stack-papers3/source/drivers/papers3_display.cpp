// SPDX-License-Identifier: Apache-2.0
#include "papers3_display.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/module.h>

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>

#define TAG "Papers3Display"
#define GET_CONFIG(device) (static_cast<const Papers3DisplayConfig*>((device)->config))

// The panel has no waveform tables. A software state machine decides per pixel and per frame
// what to drive, and a free running task streams every frame over an 8 bit parallel bus.
// Each pixel is driven toward its target color for DRIVE_FRAMES frames after the target changes
// and is left alone afterwards. Only the bounding box of recent changes is processed.

static constexpr int PANEL_WIDTH = 960;
static constexpr int PANEL_HEIGHT = 540;

static constexpr gpio_num_t PIN_PWR_EN = GPIO_NUM_45;
static constexpr gpio_num_t PIN_BST_EN = GPIO_NUM_46;
static constexpr gpio_num_t PIN_SDCE = GPIO_NUM_13;
static constexpr gpio_num_t PIN_SDLE = GPIO_NUM_15;
static constexpr gpio_num_t PIN_SDCK = GPIO_NUM_16;
static constexpr gpio_num_t PIN_GDSP = GPIO_NUM_17;
static constexpr gpio_num_t PIN_GDCK = GPIO_NUM_18;
static constexpr gpio_num_t PIN_DATA[8] = { GPIO_NUM_6, GPIO_NUM_14, GPIO_NUM_7, GPIO_NUM_12, GPIO_NUM_9, GPIO_NUM_11, GPIO_NUM_8, GPIO_NUM_10 };

// The i80 peripheral needs a data/command pin even though the panel has none
static constexpr gpio_num_t PIN_DUMMY_DC = static_cast<gpio_num_t>(49);

static constexpr uint32_t PIXEL_CLOCK_HZ = 24000000;

// A row is 2 bits per pixel, plus padding for the line latch
static constexpr int LINE_PAD_BYTES = 16;
static constexpr size_t ROW_BYTES = PANEL_WIDTH / 4 + LINE_PAD_BYTES;

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

// Full panel black and white passes that remove ghosting
static constexpr int FLASH_CYCLES = 1;
static constexpr int FLASH_FIELDS = 8;

static constexpr uint32_t POWER_SETTLE_MS = 100;
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

extern Module m5stack_papers3_module;

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

struct Papers3DisplayInternal {
    esp_lcd_i80_bus_handle_t bus;
    esp_lcd_panel_io_handle_t io;
    TaskHandle_t task;
    SemaphoreHandle_t request_mutex;
    SemaphoreHandle_t request_done;
    portMUX_TYPE lock;
    volatile bool dma_done;
    volatile bool exited;
    bool powered;
    uint32_t rotation;
    // Set by the caller of a flash request, read by the task
    bool flash_wipes_content;
    // Changes not yet picked up by the task, guarded by lock
    Region pending;
    // 1 bit per pixel in native panel orientation, 1 is black
    uint8_t* target;
    uint8_t* state;
    // Internal DMA capable rows
    uint8_t* row_buf[2];
    uint8_t* zero_row;
    uint8_t* solid_row;
};

// region Panel signalling

static bool IRAM_ATTR on_row_sent(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    static_cast<Papers3DisplayInternal*>(user_ctx)->dma_done = true;
    return false;
}

static void power_on(Papers3DisplayInternal* internal) {
    if (internal->powered) return;
    gpio_set_level(PIN_PWR_EN, 1);
    esp_rom_delay_us(100);
    gpio_set_level(PIN_BST_EN, 1);
    esp_rom_delay_us(100);
    gpio_set_level(PIN_GDCK, 1);
    gpio_set_level(PIN_GDSP, 1);
    vTaskDelay(pdMS_TO_TICKS(POWER_SETTLE_MS));
    internal->powered = true;
}

static void power_off(Papers3DisplayInternal* internal) {
    if (!internal->powered) return;
    gpio_set_level(PIN_GDCK, 0);
    gpio_set_level(PIN_GDSP, 0);
    gpio_set_level(PIN_BST_EN, 0);
    esp_rom_delay_us(100);
    gpio_set_level(PIN_PWR_EN, 0);
    esp_rom_delay_us(100);
    internal->powered = false;
}

static void IRAM_ATTR send_row(Papers3DisplayInternal* internal, const uint8_t* data) {
    while (!internal->dma_done) {}

    gpio_set_level(PIN_GDCK, 0);
    gpio_set_level(PIN_SDLE, 1);
    gpio_set_level(PIN_SDLE, 0);
    gpio_set_level(PIN_GDCK, 1);

    internal->dma_done = false;
    esp_lcd_panel_io_tx_color(internal->io, -1, data, ROW_BYTES);
}

static void IRAM_ATTR begin_frame() {
    gpio_set_level(PIN_GDCK, 1);
    esp_rom_delay_us(7);
    gpio_set_level(PIN_GDSP, 0);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_GDCK, 0);
    gpio_set_level(PIN_GDCK, 1);
    esp_rom_delay_us(8);
    gpio_set_level(PIN_GDSP, 1);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_GDCK, 0);
    for (int i = 0; i < 2; i++) {
        gpio_set_level(PIN_GDCK, 1);
        gpio_set_level(PIN_GDCK, 0);
    }
}

static void IRAM_ATTR end_frame(Papers3DisplayInternal* internal) {
    send_row(internal, internal->zero_row);
    while (!internal->dma_done) {}
}

static void pad_frame(int64_t frame_start_us) {
    const int64_t remaining_us = FRAME_PERIOD_US - (esp_timer_get_time() - frame_start_us);
    if (remaining_us > 2000) {
        vTaskDelay(pdMS_TO_TICKS((remaining_us - 1000) / 1000));
    }
    while (esp_timer_get_time() - frame_start_us < FRAME_PERIOD_US) {}
}

// endregion

// region Pixel state machine

// A state byte holds two pixels. The first (left) pixel uses bits 7:5 for its drive counter
// and bit 1 for its color, the second uses bits 4:2 and bit 0. A counter of 4 means fully driven.
// target_pair holds the 2 target color bits of the pixel pair. Returns 4 output bits for
// 2 pixels, where 01 drives to black and 10 drives to white.
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

// Encodes the columns [x0, x1) of a row, both multiples of 8. Other columns of out stay untouched.
static void IRAM_ATTR encode_row(const Papers3DisplayInternal* internal, int y, int x0, int x1, uint8_t* out) {
    const uint8_t* target = internal->target + y * TARGET_ROW_BYTES + x0 / 8;
    uint8_t* state = internal->state + y * STATE_ROW_BYTES + x0 / 2;
    out += x0 / 4;

    for (int count = (x1 - x0) / 8; count > 0; count--) {
        const uint8_t pixels = *target++;
        out[0] = static_cast<uint8_t>((drive_pair(state, pixels >> 6) << 4) | drive_pair(state + 1, (pixels >> 4) & 0x3));
        out[1] = static_cast<uint8_t>((drive_pair(state + 2, (pixels >> 2) & 0x3) << 4) | drive_pair(state + 3, pixels & 0x3));
        state += 4;
        out += 2;
    }
}

static void IRAM_ATTR run_update_frame(Papers3DisplayInternal* internal, const Region& region) {
    const int64_t start_us = esp_timer_get_time();

    // Columns outside the region must stay zero so they are not driven
    memset(internal->row_buf[0], 0, ROW_BYTES);
    memset(internal->row_buf[1], 0, ROW_BYTES);

    begin_frame();
    int current = 0;
    for (int y = 0; y < PANEL_HEIGHT; y++) {
        if (y >= region.y0 && y < region.y1) {
            // The other buffer may still be in flight while this one is encoded
            uint8_t* row = internal->row_buf[current];
            encode_row(internal, y, region.x0, region.x1, row);
            send_row(internal, row);
            current ^= 1;
        } else {
            send_row(internal, internal->zero_row);
        }
    }
    end_frame(internal);

    pad_frame(start_us);
}

// Drives every pixel toward one color. Pattern 0x55 is black and 0xAA is white.
static void run_solid_frames(Papers3DisplayInternal* internal, uint8_t pattern, int frames) {
    while (!internal->dma_done) {}
    memset(internal->solid_row, pattern, PANEL_WIDTH / 4);
    for (int frame = 0; frame < frames; frame++) {
        begin_frame();
        for (int y = 0; y < PANEL_HEIGHT; y++) {
            send_row(internal, internal->solid_row);
        }
        end_frame(internal);
    }
}

static void run_flash(Papers3DisplayInternal* internal) {
    for (int cycle = 0; cycle < FLASH_CYCLES; cycle++) {
        run_solid_frames(internal, 0x55, FLASH_FIELDS);
        run_solid_frames(internal, 0xAA, FLASH_FIELDS);
    }
}

static void display_task(void* arg) {
    auto* internal = static_cast<Papers3DisplayInternal*>(arg);
    Region active = EMPTY_REGION;
    int frames_left = 0;

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
            power_on(internal);
            run_flash(internal);
            memset(internal->state, STATE_SETTLED_WHITE, STATE_SIZE);
            active = EMPTY_REGION;
            frames_left = 0;
            portENTER_CRITICAL(&internal->lock);
            if (!internal->flash_wipes_content) {
                internal->pending = FULL_REGION;
            }
            portEXIT_CRITICAL(&internal->lock);
            xSemaphoreGive(internal->request_done);
        }

        Region pending;
        portENTER_CRITICAL(&internal->lock);
        pending = internal->pending;
        internal->pending = EMPTY_REGION;
        portEXIT_CRITICAL(&internal->lock);

        if (!pending.empty()) {
            active.merge(pending);
            frames_left = DRIVE_FRAMES;
        }

        if (frames_left > 0) {
            power_on(internal);
            // Columns are processed in groups of 8 pixels
            Region aligned = active;
            aligned.x0 &= ~7;
            aligned.x1 = (aligned.x1 + 7) & ~7;
            run_update_frame(internal, aligned);
            if (--frames_left == 0) {
                active = EMPTY_REGION;
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

static void notify_task(Papers3DisplayInternal* internal, uint32_t command) {
    xTaskNotify(internal->task, command, eSetBits);
}

// Rotated coordinates to native panel coordinates
static inline void to_native(uint32_t rotation, int x, int y, int* native_x, int* native_y) {
    switch (rotation) {
        case 90:
            *native_x = PANEL_WIDTH - 1 - y;
            *native_y = x;
            break;
        case 180:
            *native_x = PANEL_WIDTH - 1 - x;
            *native_y = PANEL_HEIGHT - 1 - y;
            break;
        case 270:
            *native_x = y;
            *native_y = PANEL_HEIGHT - 1 - x;
            break;
        default:
            *native_x = x;
            *native_y = y;
            break;
    }
}

static bool is_portrait(uint32_t rotation) {
    return rotation == 90 || rotation == 270;
}

static error_t flash_blocking(Papers3DisplayInternal* internal, bool wipe_content) {
    xSemaphoreTake(internal->request_mutex, portMAX_DELAY);
    internal->flash_wipes_content = wipe_content;
    notify_task(internal, CMD_FLASH);
    xSemaphoreTake(internal->request_done, portMAX_DELAY);
    xSemaphoreGive(internal->request_mutex);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t papers3_display_reset(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    notify_task(internal, CMD_POWER_OFF);
    return ERROR_NONE;
}

static error_t papers3_display_init(Device*) {
    return ERROR_NONE;
}

static error_t papers3_display_clear(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    memset(internal->target, 0, TARGET_SIZE);
    return flash_blocking(internal, true);
}

static error_t papers3_display_refresh(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    return flash_blocking(internal, false);
}

// Reports GRAYSCALE8 so LVGL renders partial tiles. The luminance is dithered to black and white.
static error_t papers3_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    const uint32_t rotation = internal->rotation;
    const int32_t width = x_end - x_start;
    const auto* source = static_cast<const uint8_t*>(color_data);

    for (int32_t row = 0; row < width * (y_end - y_start); row += width) {
        const int32_t y = y_start + row / width;
        for (int32_t col = 0; col < width; col++) {
            const int32_t x = x_start + col;
            int native_x, native_y;
            to_native(rotation, x, y, &native_x, &native_y);
            uint8_t* byte = internal->target + native_y * TARGET_ROW_BYTES + native_x / 8;
            const uint8_t mask = 0x80 >> (native_x & 7);
            if (dither_is_black(source[row + col], x, y)) {
                *byte |= mask;
            } else {
                *byte &= ~mask;
            }
        }
    }

    int x0, y0, x1, y1;
    to_native(rotation, x_start, y_start, &x0, &y0);
    to_native(rotation, x_end - 1, y_end - 1, &x1, &y1);
    const Region changed = { std::min(x0, x1), std::min(y0, y1), std::max(x0, x1) + 1, std::max(y0, y1) + 1 };

    portENTER_CRITICAL(&internal->lock);
    internal->pending.merge(changed);
    portEXIT_CRITICAL(&internal->lock);
    notify_task(internal, CMD_UPDATE);
    return ERROR_NONE;
}

static error_t papers3_display_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    if (!on_off) {
        notify_task(internal, CMD_POWER_OFF);
    }
    return ERROR_NONE;
}

static DisplayColorFormat papers3_display_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

static uint16_t papers3_display_get_resolution_x(Device* device) {
    return is_portrait(GET_CONFIG(device)->rotation) ? PANEL_HEIGHT : PANEL_WIDTH;
}

static uint16_t papers3_display_get_resolution_y(Device* device) {
    return is_portrait(GET_CONFIG(device)->rotation) ? PANEL_WIDTH : PANEL_HEIGHT;
}

// endregion

static constexpr DisplayApi papers3_display_api = {
    // draw_bitmap() converts into its own target buffer and never DMAs from the given pointer
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH,
    .reset = papers3_display_reset,
    .init = papers3_display_init,
    .draw_bitmap = papers3_display_draw_bitmap,
    .clear = papers3_display_clear,
    .refresh = papers3_display_refresh,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = papers3_display_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = papers3_display_get_color_format,
    .get_resolution_x = papers3_display_get_resolution_x,
    .get_resolution_y = papers3_display_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

// region Driver lifecycle

static void free_buffers(Papers3DisplayInternal* internal) {
    memory_free(internal->target);
    memory_free(internal->state);
    heap_caps_free(internal->row_buf[0]);
    heap_caps_free(internal->row_buf[1]);
    heap_caps_free(internal->zero_row);
    heap_caps_free(internal->solid_row);
}

static bool allocate_buffers(Papers3DisplayInternal* internal) {
    static const MemoryPolicy external_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 16 };
    internal->target = static_cast<uint8_t*>(memory_calloc_with_policy(1, TARGET_SIZE, &external_policy));
    internal->state = static_cast<uint8_t*>(memory_alloc_with_policy(STATE_SIZE, &external_policy));
    // Pad so DMA bursts never read past the row
    constexpr size_t dma_row_size = ROW_BYTES + 16;
    uint8_t** dma_rows[] = { &internal->row_buf[0], &internal->row_buf[1], &internal->zero_row, &internal->solid_row };
    for (uint8_t** row : dma_rows) {
        *row = static_cast<uint8_t*>(heap_caps_aligned_calloc(16, 1, dma_row_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    }
    return internal->target && internal->state && internal->row_buf[0] && internal->row_buf[1] && internal->zero_row && internal->solid_row;
}

static void configure_gpio() {
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << PIN_PWR_EN) | (1ULL << PIN_BST_EN) | (1ULL << PIN_SDLE) | (1ULL << PIN_GDSP) | (1ULL << PIN_GDCK),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&config);
}

static esp_err_t create_bus(Papers3DisplayInternal* internal) {
    esp_lcd_i80_bus_config_t bus_config = {};
    bus_config.dc_gpio_num = PIN_DUMMY_DC;
    bus_config.wr_gpio_num = PIN_SDCK;
    bus_config.clk_src = LCD_CLK_SRC_PLL160M;
    for (int i = 0; i < 8; i++) {
        bus_config.data_gpio_nums[i] = PIN_DATA[i];
    }
    bus_config.bus_width = 8;
    bus_config.max_transfer_bytes = ROW_BYTES;
    bus_config.dma_burst_size = 32;
    esp_err_t result = esp_lcd_new_i80_bus(&bus_config, &internal->bus);
    if (result != ESP_OK) return result;

    esp_lcd_panel_io_i80_config_t io_config = {};
    io_config.cs_gpio_num = PIN_SDCE;
    io_config.pclk_hz = PIXEL_CLOCK_HZ;
    io_config.trans_queue_depth = 4;
    io_config.on_color_trans_done = on_row_sent;
    io_config.user_ctx = internal;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.dc_levels.dc_data_level = 1;
    result = esp_lcd_new_panel_io_i80(internal->bus, &io_config, &internal->io);
    if (result != ESP_OK) {
        esp_lcd_del_i80_bus(internal->bus);
    }
    return result;
}

static void destroy(Papers3DisplayInternal* internal) {
    if (internal->io) esp_lcd_panel_io_del(internal->io);
    if (internal->bus) esp_lcd_del_i80_bus(internal->bus);
    if (internal->request_mutex) vSemaphoreDelete(internal->request_mutex);
    if (internal->request_done) vSemaphoreDelete(internal->request_done);
    free_buffers(internal);
    memory_free(internal);
}

static error_t start(Device* device) {
    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    auto* internal = static_cast<Papers3DisplayInternal*>(memory_calloc_with_policy(1, sizeof(Papers3DisplayInternal), &internal_policy));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->rotation = GET_CONFIG(device)->rotation;
    internal->lock = portMUX_INITIALIZER_UNLOCKED;
    internal->dma_done = true;
    internal->request_mutex = xSemaphoreCreateMutex();
    internal->request_done = xSemaphoreCreateBinary();

    if (!internal->request_mutex || !internal->request_done || !allocate_buffers(internal)) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    memset(internal->state, STATE_SETTLED_WHITE, STATE_SIZE);

    configure_gpio();
    if (create_bus(internal) != ESP_OK) {
        LOG_E(TAG, "Failed to create parallel bus");
        internal->bus = nullptr;
        internal->io = nullptr;
        destroy(internal);
        return ERROR_RESOURCE;
    }

    if (xTaskCreatePinnedToCore(display_task, "papers3_display", 4096, internal, 1, &internal->task, 1) != pdPASS) {
        LOG_E(TAG, "Failed to create display task");
        destroy(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    // The boot splash leaves residue that a regular update would never clear
    flash_blocking(internal, true);

    LOG_I(TAG, "Started (%dx%d, rotation %lu)", PANEL_WIDTH, PANEL_HEIGHT, internal->rotation);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    notify_task(internal, CMD_EXIT);
    while (!internal->exited) {
        vTaskDelay(1);
    }
    device_set_driver_data(device, nullptr);
    destroy(internal);
    return ERROR_NONE;
}

// endregion

Driver papers3_display_driver = {
    .name = "papers3-display",
    .compatible = (const char*[]) { "m5stack,papers3-display", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &papers3_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

}
