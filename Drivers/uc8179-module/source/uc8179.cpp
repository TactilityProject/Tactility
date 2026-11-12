// SPDX-License-Identifier: Apache-2.0
#include <drivers/uc8179.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/memory.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstring>
#include <initializer_list>

#define TAG "Uc8179"
#define GET_CONFIG(device) (static_cast<const Uc8179Config*>((device)->config))

// The controller keeps an old and a new image buffer. A partial update writes a window of the new
// image, refreshes that window with the partial waveform and then copies the window to the old
// buffer. Updates are done by a task, so draw_bitmap() only thresholds into a 1 bit target buffer
// and queues the changed rectangle. Rectangles that arrive close together share one update.

struct Panel {
    const char* name;
    uint16_t width;
    uint16_t height;
};

static constexpr Panel PANELS[] = {
    { "gdey075t7", 800, 480 },
};

static constexpr uint32_t SPI_CHUNK_BYTES = 4096;
static constexpr uint32_t BUSY_TIMEOUT_MS = 10000;
static constexpr uint32_t COALESCE_MS = 30;
static constexpr uint32_t RETRY_DELAY_MS = 200;
static constexpr int MAX_RETRIES = 3;

static constexpr uint32_t CMD_UPDATE = 1U << 0;
static constexpr uint32_t CMD_FULL_REFRESH = 1U << 1;
static constexpr uint32_t CMD_SLEEP = 1U << 2;
static constexpr uint32_t CMD_WAKE = 1U << 3;
static constexpr uint32_t CMD_EXIT = 1U << 4;
static constexpr uint32_t CMD_RETRY = 1U << 5;

static inline bool is_white(uint8_t luminance) {
    return luminance >= 128;
}

namespace {

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

constexpr Region EMPTY_REGION = { 0, 0, 0, 0 };

} // namespace

struct Uc8179Internal {
    Device* spi_controller;
    spi_device_handle_t spi;
    const Panel* panel;
    int pin_dc;
    int pin_reset;
    int pin_busy;
    int pin_cs;
    TaskHandle_t task;
    SemaphoreHandle_t request_mutex;
    SemaphoreHandle_t request_done;
    portMUX_TYPE lock;
    volatile bool exited;
    // 1 bit per pixel, 1 is white
    uint8_t* target;
    // Copy of the window being sent, in internal DMA capable memory
    uint8_t* snapshot;
    // Changes not yet picked up by the task, guarded by lock
    Region pending;
    // Only used by the task
    // Set when an SPI transfer failed or the panel stayed busy, cleared at the start of each update
    bool update_failed;
    // Failed updates in a row, reset by each new display request
    int retry_count;
    // Both controller buffers hold the displayed image, so partial updates are possible
    bool base_valid;
    bool asleep;
};

// region SPI

static void write_bytes(Uc8179Internal* internal, const uint8_t* data, size_t length) {
    while (length > 0) {
        const size_t chunk = std::min<size_t>(length, SPI_CHUNK_BYTES);
        spi_transaction_t transaction = {};
        transaction.length = chunk * 8;
        transaction.tx_buffer = data;
        if (spi_device_polling_transmit(internal->spi, &transaction) != ESP_OK) {
            LOG_E(TAG, "SPI transmit failed");
            internal->update_failed = true;
            return;
        }
        data += chunk;
        length -= chunk;
    }
}

static void write_command(Uc8179Internal* internal, uint8_t command) {
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 0);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_dc), 0);
    write_bytes(internal, &command, 1);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 1);
}

static void write_command(Uc8179Internal* internal, uint8_t command, std::initializer_list<uint8_t> data) {
    write_command(internal, command);
    for (const uint8_t byte : data) {
        gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 0);
        gpio_set_level(static_cast<gpio_num_t>(internal->pin_dc), 1);
        write_bytes(internal, &byte, 1);
        gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 1);
    }
}

static void write_command_with_buffer(Uc8179Internal* internal, uint8_t command, const uint8_t* data, size_t length) {
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 0);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_dc), 0);
    write_bytes(internal, &command, 1);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_dc), 1);
    write_bytes(internal, data, length);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 1);
}

// The busy pin is low while the controller is busy
static void wait_busy(Uc8179Internal* internal) {
    const TickType_t start = xTaskGetTickCount();
    while (!gpio_get_level(static_cast<gpio_num_t>(internal->pin_busy))) {
        if (xTaskGetTickCount() - start > pdMS_TO_TICKS(BUSY_TIMEOUT_MS)) {
            LOG_W(TAG, "Timeout waiting for panel");
            internal->update_failed = true;
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// endregion

// region Panel

static void reset_panel(Uc8179Internal* internal) {
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_reset), 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_reset), 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void init_panel(Uc8179Internal* internal) {
    const Panel& panel = *internal->panel;
    // Panel setting: black and white, waveform from OTP
    write_command(internal, 0x00, { 0x1F });
    // Power setting, same values as the panel's OTP defaults
    write_command(internal, 0x01, { 0x07, 0x07, 0x3F, 0x3F, 0x09 });
    // Booster soft start
    write_command(internal, 0x06, { 0x17, 0x17, 0x28, 0x17 });
    write_command(internal, 0x61, {
        static_cast<uint8_t>(panel.width >> 8),
        static_cast<uint8_t>(panel.width & 0xFF),
        static_cast<uint8_t>(panel.height >> 8),
        static_cast<uint8_t>(panel.height & 0xFF),
    });
    // Disable the dual SPI interface
    write_command(internal, 0x15, { 0x00 });
    // VCOM and data interval setting, copies the new image to the old buffer on refresh
    write_command(internal, 0x50, { 0x29, 0x07 });
    write_command(internal, 0x60, { 0x22 });
    // Power saving: VCOM 2 line period, source 2x 660ns
    write_command(internal, 0xE3, { 0x22 });
}

static void power_on(Uc8179Internal* internal) {
    write_command(internal, 0x04);
    spi_controller_unlock(internal->spi_controller);
    wait_busy(internal);
    spi_controller_lock(internal->spi_controller);
}

static void power_off(Uc8179Internal* internal) {
    write_command(internal, 0x02);
    spi_controller_unlock(internal->spi_controller);
    wait_busy(internal);
    spi_controller_lock(internal->spi_controller);
}

static void start_refresh(Uc8179Internal* internal) {
    write_command(internal, 0x12);
    spi_controller_unlock(internal->spi_controller);
    // The busy pin is only valid 200 microseconds after the refresh command
    vTaskDelay(pdMS_TO_TICKS(1));
    wait_busy(internal);
    spi_controller_lock(internal->spi_controller);
}

// Sets the partial window. x0 and x1 are in pixels and multiples of 8, per PTL (0x90)
static void set_window(Uc8179Internal* internal, const Region& region) {
    const int last_col = region.x1 - 1;
    const int last_row = region.y1 - 1;
    write_command(internal, 0x90, {
        static_cast<uint8_t>(region.x0 >> 8),
        static_cast<uint8_t>(region.x0 & 0xFF),
        static_cast<uint8_t>(last_col >> 8),
        static_cast<uint8_t>(last_col & 0xFF),
        static_cast<uint8_t>(region.y0 >> 8),
        static_cast<uint8_t>(region.y0 & 0xFF),
        static_cast<uint8_t>(last_row >> 8),
        static_cast<uint8_t>(last_row & 0xFF),
        0x01,
    });
}

// Copies a window of the target into the snapshot. Returns its size in bytes.
static size_t snapshot_window(Uc8179Internal* internal, const Region& region) {
    const size_t stride = internal->panel->width / 8;
    const size_t row_bytes = (region.x1 - region.x0) / 8;
    uint8_t* out = internal->snapshot;
    for (int y = region.y0; y < region.y1; y++) {
        memcpy(out, internal->target + y * stride + region.x0 / 8, row_bytes);
        out += row_bytes;
    }
    return row_bytes * (region.y1 - region.y0);
}

// Writes the window of the snapshot to the old or new image buffer
static void write_window(Uc8179Internal* internal, uint8_t buffer_command, const Region& region, size_t size) {
    write_command(internal, 0x91);
    set_window(internal, region);
    write_command_with_buffer(internal, buffer_command, internal->snapshot, size);
    write_command(internal, 0x92);
}

// Redraws the whole panel. The fast waveform takes about a second and ghosts. The slow
// waveform takes longer and clears ghosting.
// Returns false when a transfer failed, which leaves the base image invalid.
static bool full_update(Uc8179Internal* internal, bool fast) {
    const Panel& panel = *internal->panel;
    const Region full = { 0, 0, panel.width, panel.height };
    const size_t size = snapshot_window(internal, full);
    internal->update_failed = false;

    spi_controller_lock(internal->spi_controller);
    init_panel(internal);
    write_command_with_buffer(internal, 0x10, internal->snapshot, size);
    write_command_with_buffer(internal, 0x13, internal->snapshot, size);
    if (internal->update_failed) {
        spi_controller_unlock(internal->spi_controller);
        internal->base_valid = false;
        return false;
    }
    if (fast) {
        // Forced temperature selects the fast waveform
        write_command(internal, 0xE0, { 0x02 });
        write_command(internal, 0xE5, { 0x5A });
    } else {
        // Clears any forced temperature from a previous fast update, so the controller uses
        // its internal sensor and the slow waveform for this (ghost-clearing) refresh
        write_command(internal, 0xE0, { 0x00 });
        write_command(internal, 0x41, { 0x00 });
    }
    power_on(internal);
    if (internal->update_failed) {
        spi_controller_unlock(internal->spi_controller);
        internal->base_valid = false;
        return false;
    }
    start_refresh(internal);
    power_off(internal);
    spi_controller_unlock(internal->spi_controller);

    internal->base_valid = !internal->update_failed;
    return internal->base_valid;
}

// Refreshes only the window, which takes about 0.45 seconds
// Returns false when a transfer failed, which leaves the base image invalid.
static bool partial_update(Uc8179Internal* internal, const Region& region) {
    const size_t size = snapshot_window(internal, region);
    internal->update_failed = false;

    spi_controller_lock(internal->spi_controller);
    init_panel(internal);
    write_window(internal, 0x13, region, size);
    if (internal->update_failed) {
        spi_controller_unlock(internal->spi_controller);
        internal->base_valid = false;
        return false;
    }

    write_command(internal, 0x91);
    set_window(internal, region);
    // Forced temperature selects the partial waveform
    write_command(internal, 0xE0, { 0x02 });
    write_command(internal, 0xE5, { 0x6E });
    power_on(internal);
    if (internal->update_failed) {
        spi_controller_unlock(internal->spi_controller);
        internal->base_valid = false;
        return false;
    }
    start_refresh(internal);
    write_command(internal, 0x92);
    power_off(internal);

    // The next partial update compares against what is displayed now
    init_panel(internal);
    write_window(internal, 0x10, region, size);
    spi_controller_unlock(internal->spi_controller);

    internal->base_valid = !internal->update_failed;
    return internal->base_valid;
}

static void enter_sleep(Uc8179Internal* internal) {
    if (internal->asleep) return;
    // Only a hardware reset wakes the controller from deep sleep
    if (internal->pin_reset >= 0) {
        spi_controller_lock(internal->spi_controller);
        write_command(internal, 0x07, { 0xA5 });
        spi_controller_unlock(internal->spi_controller);
    }
    internal->asleep = true;
    internal->base_valid = false;
}

static void leave_sleep(Uc8179Internal* internal) {
    if (!internal->asleep) return;
    // Deep sleep loses the buffers, so the next update redraws the whole target
    if (internal->pin_reset >= 0) {
        reset_panel(internal);
    }
    internal->asleep = false;
    internal->base_valid = false;
    const Region full = { 0, 0, internal->panel->width, internal->panel->height };
    portENTER_CRITICAL(&internal->lock);
    internal->pending.merge(full);
    portEXIT_CRITICAL(&internal->lock);
}

// Queues the whole target again after a failed transfer, with a growing delay. After
// MAX_RETRIES the next display request tries again.
static void retry_update(Uc8179Internal* internal) {
    const Region full = { 0, 0, internal->panel->width, internal->panel->height };
    portENTER_CRITICAL(&internal->lock);
    internal->pending.merge(full);
    portEXIT_CRITICAL(&internal->lock);
    if (internal->retry_count >= MAX_RETRIES) {
        LOG_E(TAG, "Giving up after %d retries", MAX_RETRIES);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS << internal->retry_count));
    internal->retry_count++;
    xTaskNotify(internal->task, CMD_RETRY, eSetBits);
}

static void update_task(void* arg) {
    auto* internal = static_cast<Uc8179Internal*>(arg);

    while (true) {
        uint32_t commands = 0;
        xTaskNotifyWait(0, UINT32_MAX, &commands, portMAX_DELAY);

        if (commands & CMD_EXIT) {
            // The panel keeps its image in deep sleep
            enter_sleep(internal);
            break;
        }

        if (commands & CMD_SLEEP) {
            enter_sleep(internal);
        }

        if (commands & (CMD_UPDATE | CMD_FULL_REFRESH)) {
            internal->retry_count = 0;
        }

        if (commands & CMD_WAKE) {
            leave_sleep(internal);
        }

        if (commands & CMD_FULL_REFRESH) {
            if (!internal->asleep) {
                // Cleared before the snapshot, so draws during the refresh stay queued
                portENTER_CRITICAL(&internal->lock);
                internal->pending = EMPTY_REGION;
                portEXIT_CRITICAL(&internal->lock);
                if (!full_update(internal, false)) {
                    retry_update(internal);
                }
            }
            xSemaphoreGive(internal->request_done);
        }

        if (internal->asleep) {
            continue;
        }

        if (commands & CMD_UPDATE) {
            // Rectangles of one redraw arrive within a few milliseconds
            vTaskDelay(pdMS_TO_TICKS(COALESCE_MS));
        }

        Region pending;
        portENTER_CRITICAL(&internal->lock);
        pending = internal->pending;
        internal->pending = EMPTY_REGION;
        portEXIT_CRITICAL(&internal->lock);

        if (!pending.empty()) {
            const bool ok = internal->base_valid ? partial_update(internal, pending) : full_update(internal, true);
            if (!ok) {
                retry_update(internal);
            }
        }
    }

    internal->exited = true;
    vTaskDelete(nullptr);
}

// endregion

// region Helpers

static void notify_task(Uc8179Internal* internal, uint32_t command) {
    xTaskNotify(internal->task, command, eSetBits);
}

static error_t full_refresh_blocking(Uc8179Internal* internal) {
    xSemaphoreTake(internal->request_mutex, portMAX_DELAY);
    notify_task(internal, CMD_FULL_REFRESH);
    xSemaphoreTake(internal->request_done, portMAX_DELAY);
    xSemaphoreGive(internal->request_mutex);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t uc8179_reset(Device* device) {
    auto* internal = static_cast<Uc8179Internal*>(device_get_driver_data(device));
    notify_task(internal, CMD_SLEEP | CMD_WAKE);
    return ERROR_NONE;
}

static error_t uc8179_init(Device*) {
    return ERROR_NONE;
}

// Reports GRAYSCALE8 so LVGL renders partial tiles. The luminance is thresholded to black and white.
static error_t uc8179_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Uc8179Internal*>(device_get_driver_data(device));
    const size_t stride = internal->panel->width / 8;
    const int32_t width = x_end - x_start;
    const auto* source = static_cast<const uint8_t*>(color_data);

    for (int32_t row = 0; row < y_end - y_start; row++) {
        uint8_t* target_row = internal->target + (y_start + row) * stride;
        for (int32_t col = 0; col < width; col++) {
            const int32_t x = x_start + col;
            const uint8_t mask = 0x80 >> (x & 7);
            if (is_white(source[row * width + col])) {
                target_row[x / 8] |= mask;
            } else {
                target_row[x / 8] &= ~mask;
            }
        }
    }

    // Windows are in whole bytes horizontally
    const Region changed = { x_start & ~7, y_start, (x_end + 7) & ~7, y_end };
    portENTER_CRITICAL(&internal->lock);
    internal->pending.merge(changed);
    portEXIT_CRITICAL(&internal->lock);
    notify_task(internal, CMD_UPDATE);
    return ERROR_NONE;
}

static error_t uc8179_clear(Device* device) {
    auto* internal = static_cast<Uc8179Internal*>(device_get_driver_data(device));
    memset(internal->target, 0xFF, internal->panel->width / 8 * internal->panel->height);
    return full_refresh_blocking(internal);
}

static error_t uc8179_refresh(Device* device) {
    auto* internal = static_cast<Uc8179Internal*>(device_get_driver_data(device));
    return full_refresh_blocking(internal);
}

static error_t uc8179_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Uc8179Internal*>(device_get_driver_data(device));
    notify_task(internal, on_off ? CMD_WAKE : CMD_SLEEP);
    return ERROR_NONE;
}

static DisplayColorFormat uc8179_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

static uint16_t uc8179_get_resolution_x(Device* device) {
    return static_cast<Uc8179Internal*>(device_get_driver_data(device))->panel->width;
}

static uint16_t uc8179_get_resolution_y(Device* device) {
    return static_cast<Uc8179Internal*>(device_get_driver_data(device))->panel->height;
}

// endregion

static const DisplayApi uc8179_display_api = {
    // draw_bitmap() converts into its own target buffer and never DMAs from the given pointer
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
    .reset = uc8179_reset,
    .init = uc8179_init,
    .draw_bitmap = uc8179_draw_bitmap,
    .clear = uc8179_clear,
    .refresh = uc8179_refresh,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = uc8179_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = uc8179_get_color_format,
    .get_resolution_x = uc8179_get_resolution_x,
    .get_resolution_y = uc8179_get_resolution_y,
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = nullptr,
    .has_capability = nullptr,
    .get_shape = nullptr,
};

// region Driver lifecycle

static const Panel* find_panel(const char* name) {
    for (const Panel& panel : PANELS) {
        if (strcmp(panel.name, name) == 0) {
            return &panel;
        }
    }
    return nullptr;
}

static void configure_gpio(const Uc8179Internal* internal) {
    gpio_config_t outputs = {
        .pin_bit_mask = (1ULL << internal->pin_dc) | (1ULL << internal->pin_cs),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (internal->pin_reset >= 0) {
        outputs.pin_bit_mask |= 1ULL << internal->pin_reset;
    }
    gpio_config(&outputs);
    gpio_set_level(static_cast<gpio_num_t>(internal->pin_cs), 1);
    if (internal->pin_reset >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(internal->pin_reset), 1);
    }

    gpio_config_t input = {
        .pin_bit_mask = 1ULL << internal->pin_busy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&input);
}

static void destroy(Uc8179Internal* internal) {
    if (internal->spi != nullptr) spi_bus_remove_device(internal->spi);
    if (internal->request_mutex != nullptr) vSemaphoreDelete(internal->request_mutex);
    if (internal->request_done != nullptr) vSemaphoreDelete(internal->request_done);
    memory_free(internal->target);
    heap_caps_free(internal->snapshot);
    memory_free(internal);
}

static error_t start(Device* device) {
    Device* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    const Panel* panel = find_panel(config->panel_type);
    if (panel == nullptr) {
        LOG_E(TAG, "Unknown panel type: %s", config->panel_type);
        return ERROR_NOT_SUPPORTED;
    }
    if (config->clock_speed_hz <= 0) {
        LOG_E(TAG, "Invalid clock speed");
        return ERROR_NOT_SUPPORTED;
    }

    static const MemoryPolicy internal_policy = { .required = MEMORY_CAPABILITY_INTERNAL, .desired = 0, .alignment = 0 };
    static const MemoryPolicy external_policy = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };
    auto* internal = static_cast<Uc8179Internal*>(memory_calloc_with_policy(1, sizeof(Uc8179Internal), &internal_policy));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    const size_t frame_bytes = panel->width / 8 * panel->height;
    internal->spi_controller = parent;
    internal->panel = panel;
    internal->pin_dc = config->pin_dc.pin;
    internal->pin_reset = config->pin_reset.gpio_controller != nullptr ? static_cast<int>(config->pin_reset.pin) : -1;
    internal->pin_busy = config->pin_busy.pin;
    internal->pin_cs = config->pin_cs.pin;
    internal->lock = portMUX_INITIALIZER_UNLOCKED;
    internal->request_mutex = xSemaphoreCreateMutex();
    internal->request_done = xSemaphoreCreateBinary();
    internal->target = static_cast<uint8_t*>(memory_alloc_with_policy(frame_bytes, &external_policy));
    internal->snapshot = static_cast<uint8_t*>(heap_caps_malloc(frame_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!internal->request_mutex || !internal->request_done || !internal->target || !internal->snapshot) {
        destroy(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    memset(internal->target, 0xFF, frame_bytes);

    configure_gpio(internal);

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = config->clock_speed_hz;
    device_config.mode = 0;
    device_config.spics_io_num = -1;
    device_config.queue_size = 1;
    if (spi_bus_add_device(spi_config->host, &device_config, &internal->spi) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device");
        internal->spi = nullptr;
        destroy(internal);
        return ERROR_RESOURCE;
    }

    if (xTaskCreate(update_task, "uc8179", 4096, internal, 4, &internal->task) != pdPASS) {
        LOG_E(TAG, "Failed to create update task");
        destroy(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    LOG_I(TAG, "Started %s %ux%u", panel->name, panel->width, panel->height);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Uc8179Internal*>(device_get_driver_data(device));

    notify_task(internal, CMD_EXIT);
    while (!internal->exited) {
        vTaskDelay(1);
    }

    device_set_driver_data(device, nullptr);
    destroy(internal);
    return ERROR_NONE;
}

// endregion

Driver uc8179_driver = {
    .name = "uc8179",
    .compatible = (const char*[]) { "ultrachip,uc8179", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &uc8179_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};
