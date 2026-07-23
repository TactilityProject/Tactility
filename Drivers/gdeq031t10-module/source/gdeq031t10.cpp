// SPDX-License-Identifier: Apache-2.0
#include <drivers/gdeq031t10.h>
#include <gdeq031t10_module.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/time.h>

#include <driver/spi_master.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "GDEQ031T10";
#define GET_CONFIG(device) (static_cast<const Gdeq031t10Config*>((device)->config))

static constexpr int WIDTH = 240;
static constexpr int HEIGHT = 320;
static constexpr size_t FRAMEBUFFER_SIZE = (WIDTH * HEIGHT) / 8; // 1 bpp packed
static constexpr int BYTES_PER_ROW = WIDTH / 8;

// UC8253-family commands, ported from Xinyuan-LilyGO/T-Deck-MAX's
// Display_EPD_W21.cpp reference driver.
static constexpr uint8_t CMD_PANEL_SETTING = 0x00;
static constexpr uint8_t CMD_POWER_ON_OFF = 0x02; // shared opcode: power on when followed by 0x04, power off as standalone 0x02
static constexpr uint8_t CMD_POWER_ON = 0x04;
static constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
static constexpr uint8_t CMD_DATA_START_OLD = 0x10;
static constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
static constexpr uint8_t CMD_DATA_START_NEW = 0x13;
static constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
static constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
static constexpr uint8_t CMD_PARTIAL_IN = 0x91;
static constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
static constexpr uint8_t CMD_FAST_MODE_ENABLE = 0xE0;
static constexpr uint8_t CMD_FAST_MODE_TIMING = 0xE5;

static constexpr uint8_t DEEP_SLEEP_CHECK_CODE = 0xA5;

extern "C" {

struct Gdeq031t10Internal {
    spi_device_handle_t spi_device;
    struct GpioDescriptor* dc;
    struct GpioDescriptor* reset; // optional
    struct GpioDescriptor* busy;
    /** Mirrors what the panel currently holds, required by the controller's
     * "old data" + "new data" double-buffered refresh protocol. Panel polarity
     * matches the LVGL 1bpp render data directly (bit 1 = white, bit 0 = black). */
    uint8_t* shadow_framebuffer;
    /** Scratch buffer used to gather a windowed region's bytes for one SPI write. */
    uint8_t* region_buffer;
    /** Serializes panel/SPI access between draw_bitmap and power on/off calls. */
    SemaphoreHandle_t panel_mutex;
    /** Waveform mode the panel registers currently hold; valid=false when the
     * registers are in an unknown state (before first init, or after deep sleep,
     * which requires a reset that restores defaults). */
    enum Gdeq031t10RefreshMode panel_mode;
    bool panel_mode_valid;
    /** True while the panel's charge pump is on (CMD_POWER_ON issued, no power-off since). */
    bool panel_power_on;
    /** disp_on_off state; the panel is in deep sleep while false. */
    bool display_on;
    /** Forces the next refresh to be a full-screen refresh (set at boot and by reset()). */
    bool force_full_refresh;
};

// region Panel protocol

static bool write_command(Gdeq031t10Internal* internal, uint8_t command) {
    gpio_descriptor_set_level(internal->dc, false);
    spi_transaction_t transaction = {};
    transaction.length = 8;
    transaction.tx_buffer = &command;
    if (spi_device_polling_transmit(internal->spi_device, &transaction) != ESP_OK) {
        LOG_E(TAG, "SPI command transfer failed");
        return false;
    }
    return true;
}

static bool write_data(Gdeq031t10Internal* internal, const uint8_t* data, size_t length) {
    gpio_descriptor_set_level(internal->dc, true);
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    transaction.tx_buffer = data;
    if (spi_device_polling_transmit(internal->spi_device, &transaction) != ESP_OK) {
        LOG_E(TAG, "SPI data transfer failed");
        return false;
    }
    return true;
}

static bool write_data_byte(Gdeq031t10Internal* internal, uint8_t data) {
    return write_data(internal, &data, 1);
}

static bool wait_while_busy(Gdeq031t10Internal* internal) {
    // gpio_descriptor_get_level() reports the panel's busy state directly (true = busy).
    const TickType_t timeout = pdMS_TO_TICKS(5000);
    const TickType_t start = get_ticks();
    bool busy = true;
    while (gpio_descriptor_get_level(internal->busy, &busy) == ERROR_NONE && busy) {
        if (get_ticks() - start > timeout) {
            LOG_E(TAG, "Timed out waiting for panel BUSY");
            return false;
        }
        delay_millis(2);
    }
    LOG_I(TAG, "waited %lu ms until not busy", get_ticks() - start);
    return !busy;
}

static void hardware_reset(Gdeq031t10Internal* internal) {
    if (internal->reset != nullptr) {
        gpio_descriptor_set_level(internal->reset, false);
        delay_millis(10);
        gpio_descriptor_set_level(internal->reset, true);
        delay_millis(10);
    }
}

static bool init_full(Gdeq031t10Internal* internal, bool mirror_180) {
    bool ok = write_command(internal, CMD_PANEL_SETTING);
    ok = ok && write_data_byte(internal, mirror_180 ? 0x13 : 0x1F);
    ok = ok && write_command(internal, CMD_POWER_ON);
    ok = ok && wait_while_busy(internal);
    if (!ok) {
        LOG_E(TAG, "Full init failed");
        return false;
    }
    internal->panel_mode = GDEQ031T10_REFRESH_FULL;
    internal->panel_mode_valid = true;
    internal->panel_power_on = true;
    return true;
}

static bool init_with_fast_lut(Gdeq031t10Internal* internal, bool mirror_180, enum Gdeq031t10RefreshMode mode) {
    if (!init_full(internal, mirror_180)) {
        return false;
    }
    // Fast-LUT timing values from the vendor reference driver: ~1.0s (fast),
    // ~1.5s (slow, extra settling) and the partial-mode value.
    uint8_t timing;
    switch (mode) {
        case GDEQ031T10_REFRESH_FAST: timing = 0x5A; break;
        case GDEQ031T10_REFRESH_SLOW: timing = 0x6E; break;
        case GDEQ031T10_REFRESH_PARTIAL: timing = 0x79; break;
        default: return true; // GDEQ031T10_REFRESH_FULL: init_full() already did everything
    }
    bool ok = write_command(internal, CMD_FAST_MODE_ENABLE);
    ok = ok && write_data_byte(internal, 0x02);
    ok = ok && write_command(internal, CMD_FAST_MODE_TIMING);
    ok = ok && write_data_byte(internal, timing);
    if (ok && mode == GDEQ031T10_REFRESH_PARTIAL) {
        ok = write_command(internal, CMD_VCOM_DATA_INTERVAL);
        ok = ok && write_data_byte(internal, 0xD7);
    }
    if (!ok) {
        LOG_E(TAG, "Mode init failed");
        return false;
    }
    internal->panel_mode = mode;
    return true;
}

static bool ensure_panel_ready(Gdeq031t10Internal* internal, bool mirror_180, enum Gdeq031t10RefreshMode mode) {
    if (!internal->panel_mode_valid || internal->panel_mode != mode) {
        return init_with_fast_lut(internal, mirror_180, mode);
    } else if (!internal->panel_power_on) {
        // Registers still hold the mode; only the charge pump was idled.
        if (!write_command(internal, CMD_POWER_ON) || !wait_while_busy(internal)) {
            LOG_E(TAG, "Panel did not become ready after power-on");
            return false;
        }
        internal->panel_power_on = true;
    }
    return true;
}

static bool panel_power_off(Gdeq031t10Internal* internal) {
    // Command the panel off regardless of whether BUSY confirms it: retrying
    // forever here would just as likely hang, and a stuck-BUSY panel is
    // already unusable either way.
    bool ok = write_command(internal, CMD_POWER_ON_OFF); // 0x02 standalone = power off
    if (!ok || !wait_while_busy(internal)) {
        LOG_E(TAG, "Panel did not confirm power-off");
        ok = false;
    }
    internal->panel_power_on = false;
    return ok;
}

static void refresh_full(Gdeq031t10Internal* internal, bool mirror_180, enum Gdeq031t10RefreshMode mode, const uint8_t* render_bitmap) {
    // Always do a full register reload before this update, and put the panel back into
    // deep sleep after it, instead of caching/reusing state across updates the way
    // ensure_panel_ready() does when the mode hasn't changed. This matches the vendor reference
    // driver (Xinyuan-LilyGO/T-Deck-MAX's Display_EPD_W21.cpp) exactly: it calls
    // EPD_Init()/EPD_Init_Fast() before every single update and EPD_DeepSleep() after every
    // single update, never reusing controller state between them. Reusing state - or tracking
    // real old-data without also doing this, or vice versa - each independently left the panel
    // showing an intermittent/alternating blank screen; only the combination matches what the
    // panel's internal state machine tolerates.
    if (!init_with_fast_lut(internal, mirror_180, mode)) {
        LOG_E(TAG, "Skipping full refresh: panel not ready");
        return;
    }

    // shadow_framebuffer holds the panel's actual current content (matches the vendor's tracked
    // oldData[] buffer) - send it as "old" data so the controller computes correct per-pixel
    // transitions.
    LOG_I(TAG, "write old data from shadow_buffer");
    if (!write_command(internal, CMD_DATA_START_OLD) || !write_data(internal, internal->shadow_framebuffer, FRAMEBUFFER_SIZE)) {
        LOG_E(TAG, "Failed to send old frame data");
        return;
    }

    LOG_I(TAG, "memcpy render_bitmap -> shadow_buffer");
    std::memcpy(internal->shadow_framebuffer, render_bitmap, FRAMEBUFFER_SIZE);
    if (!write_command(internal, CMD_DATA_START_NEW) || !write_data(internal, internal->shadow_framebuffer, FRAMEBUFFER_SIZE)) {
        LOG_E(TAG, "Failed to send new frame data");
        return;
    }

    if (!write_command(internal, CMD_DISPLAY_REFRESH)) {
        LOG_E(TAG, "Failed to trigger display refresh");
        return;
    }
    delay_millis(1); // datasheet requires >=200us settle before polling BUSY
    if (!wait_while_busy(internal)) {
        LOG_E(TAG, "Full refresh did not complete");
    }

    // EPD_DeepSleep(): power off, then actually deep-sleep rather than just idling the charge
    // pump. panel_mode_valid=false forces the next refresh_full() call back through a fresh
    // init above instead of reusing (possibly stale) controller state.
    if (internal->panel_power_on) {
        panel_power_off(internal);
    }
}

/** Windowed partial refresh of the given byte-column/row bounding box. */
static void refresh_window(Gdeq031t10Internal* internal, bool mirror_180, const uint8_t* render_bitmap, int first_byte_col, int last_byte_col, int first_row, int last_row) {
    // Partial LUT (fast waveform via temperature force) on a sub-region only. The
    // RAM window must be byte-aligned in X, which it already is (byte columns).
    if (!ensure_panel_ready(internal, mirror_180, GDEQ031T10_REFRESH_PARTIAL)) {
        LOG_E(TAG, "Skipping window refresh: panel not ready");
        return;
    }

    const int width_bytes = last_byte_col - first_byte_col + 1;

    const uint16_t x = static_cast<uint16_t>(first_byte_col * 8);
    const uint16_t xe = static_cast<uint16_t>(last_byte_col * 8 + 7);
    const uint16_t y = static_cast<uint16_t>(first_row);
    const uint16_t ye = static_cast<uint16_t>(last_row);

    // Set the partial RAM window (GxEPD2 GDEQ031T10 sequence).
    bool ok = write_command(internal, CMD_PARTIAL_IN);
    ok = ok && write_command(internal, CMD_PARTIAL_WINDOW);
    ok = ok && write_data_byte(internal, static_cast<uint8_t>(x));
    ok = ok && write_data_byte(internal, static_cast<uint8_t>(xe));
    ok = ok && write_data_byte(internal, static_cast<uint8_t>(y >> 8));
    ok = ok && write_data_byte(internal, static_cast<uint8_t>(y & 0xFF));
    ok = ok && write_data_byte(internal, static_cast<uint8_t>(ye >> 8));
    ok = ok && write_data_byte(internal, static_cast<uint8_t>(ye & 0xFF));
    ok = ok && write_data_byte(internal, 0x01);
    if (!ok) {
        LOG_E(TAG, "Failed to set partial refresh window");
        write_command(internal, CMD_PARTIAL_OUT); // best-effort: leave partial-window mode
        return;
    }

    // Old region: the region's current panel contents (shadow), gathered contiguously.
    size_t n = 0;
    for (int row = first_row; row <= last_row; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW + first_byte_col;
        std::memcpy(&internal->region_buffer[n], &internal->shadow_framebuffer[base], width_bytes);
        n += width_bytes;
    }
    if (!write_command(internal, CMD_DATA_START_OLD) || !write_data(internal, internal->region_buffer, n)) {
        LOG_E(TAG, "Failed to send old window data");
        write_command(internal, CMD_PARTIAL_OUT);
        return;
    }

    // New region: render data, and update the shadow for this region as we go.
    n = 0;
    for (int row = first_row; row <= last_row; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW + first_byte_col;
        for (int c = 0; c < width_bytes; c++) {
            const uint8_t value = render_bitmap[base + c];
            internal->region_buffer[n++] = value;
            internal->shadow_framebuffer[base + c] = value;
        }
    }
    if (!write_command(internal, CMD_DATA_START_NEW) || !write_data(internal, internal->region_buffer, n)) {
        LOG_E(TAG, "Failed to send new window data");
        write_command(internal, CMD_PARTIAL_OUT);
        return;
    }

    if (write_command(internal, CMD_DISPLAY_REFRESH)) {
        delay_millis(1);
        if (!wait_while_busy(internal)) {
            LOG_E(TAG, "Window refresh did not complete");
        }
    } else {
        LOG_E(TAG, "Failed to trigger window refresh");
    }
    write_command(internal, CMD_PARTIAL_OUT);
}

// endregion

// region DisplayApi

static error_t gdeq031t10_reset(Device* device) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    // The reset pulse restores register defaults and drops the charge pump.
    hardware_reset(internal);
    internal->panel_mode_valid = false;
    internal->panel_power_on = false;
    internal->force_full_refresh = true;
    xSemaphoreGive(internal->panel_mutex);
    return ERROR_NONE;
}

static error_t gdeq031t10_init(Device* device) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    bool ok = init_full(internal, config->mirror_180);
    xSemaphoreGive(internal->panel_mutex);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

// LVGL only ever calls this with the full frame: DISPLAY_COLOR_FORMAT_MONOCHROME forces
// LV_DISPLAY_RENDER_MODE_FULL in the generic kernel LVGL bridge (lvgl_display.c), and FULL mode
// only presents (calls draw_bitmap) once per render cycle, with the complete 0,0..hres,vres rect.
// color_data is row-major, MSB-first 1bpp (LVGL's LV_COLOR_FORMAT_I1 with the palette header
// already stripped by the caller); bit 1 = white, bit 0 = black. The panel expects the same
// polarity, so color_data is written to the panel unmodified.
static error_t gdeq031t10_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    LOG_I(TAG, "draw_bitmap %d %d - %d %d", x_start, y_start, x_end, y_end);
    if (x_start != 0 || y_start != 0 || x_end != WIDTH || y_end != HEIGHT) {
        LOG_I(TAG, "draw_bitmap: Only full-frame draws are supported (got %ld,%ld..%ld,%ld)", (long)x_start, (long)y_start, (long)x_end, (long)y_end);
        return ERROR_NOT_SUPPORTED;
    }

    const auto* render_bitmap = static_cast<const uint8_t*>(color_data);

    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);

    // Work-around until partial updates are working
    internal->force_full_refresh = true;

    if (!internal->display_on) {
        // Display is off (deep sleep). Drop the frame without touching the shadow: the
        // mismatch it leaves behind makes the frame redraw after the next power-on.
        xSemaphoreGive(internal->panel_mutex);
        LOG_W(TAG, "draw_bitmap: ignoring drawing, display is off");
        return ERROR_NONE;
    }

    // Find the bounding box of bytes that differ from what the panel holds (shadow
    // holds the same polarity as render_bitmap).
    int first_col = BYTES_PER_ROW, last_col = -1, first_row = HEIGHT, last_row = -1;
    for (int row = 0; row < HEIGHT; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW;
        for (int col = 0; col < BYTES_PER_ROW; col++) {
            if (render_bitmap[base + col] != internal->shadow_framebuffer[base + col]) {
                if (col < first_col) first_col = col;
                if (col > last_col) last_col = col;
                if (row < first_row) first_row = row;
                if (row > last_row) last_row = row;
            }
        }
    }

    const bool nothing_changed = (last_col < 0);
    if (nothing_changed && !internal->force_full_refresh) {
        xSemaphoreGive(internal->panel_mutex);
        return ERROR_NONE; // panel already shows this frame; don't refresh needlessly
    }

    if (internal->force_full_refresh) {
        LOG_I(TAG, "draw_bitmap: refresh_full");
        refresh_full(internal, config->mirror_180, config->refresh_mode, render_bitmap);
        internal->force_full_refresh = false;
    } else {
        LOG_I(TAG, "draw_bitmap: refresh_window");
        refresh_window(internal, config->mirror_180, render_bitmap, first_col, last_col, first_row, last_row);
    }

    // The refresh is synchronous, so the pipeline is idle here: drop the charge pump rather
    // than leave it running. The mode registers survive a power-off, so the next refresh
    // only pays a short power-on wait.
    if (internal->panel_power_on) {
        panel_power_off(internal);
    }

    xSemaphoreGive(internal->panel_mutex);
    return ERROR_NONE;
}

static error_t gdeq031t10_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    if (on_off == internal->display_on) {
        xSemaphoreGive(internal->panel_mutex);
        return ERROR_NONE;
    }

    bool ok = true;
    if (on_off) {
        // Toggling RST (in init_full) also wakes the panel from deep sleep. The panel kept
        // its image through deep sleep and the shadow still matches it, so no forced
        // refresh is needed: any frame dropped while off left a shadow mismatch that the
        // next draw picks up.
        ok = init_full(internal, config->mirror_180);
        if (ok && internal->panel_power_on) {
            // init_full left the charge pump on; idle it until the next draw needs it.
            panel_power_off(internal);
        }
    } else {
        if (internal->panel_power_on) {
            panel_power_off(internal);
        }
        delay_millis(100);
        write_command(internal, CMD_DEEP_SLEEP);
        write_data_byte(internal, DEEP_SLEEP_CHECK_CODE);
        // Deep sleep needs a reset to wake, which restores register defaults.
        internal->panel_mode_valid = false;
    }

    internal->display_on = on_off;
    xSemaphoreGive(internal->panel_mutex);
    return ok ? ERROR_NONE : ERROR_RESOURCE;
}

static DisplayColorFormat gdeq031t10_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_MONOCHROME;
}

static uint16_t gdeq031t10_get_resolution_x(Device*) {
    return WIDTH;
}

static uint16_t gdeq031t10_get_resolution_y(Device*) {
    return HEIGHT;
}

static void gdeq031t10_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t gdeq031t10_get_frame_buffer_count(Device*) {
    return 0;
}

// endregion

static const DisplayApi gdeq031t10_display_api = {
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH,
    .reset = gdeq031t10_reset,
    .init = gdeq031t10_init,
    .draw_bitmap = gdeq031t10_draw_bitmap,
    .mirror = nullptr,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = nullptr,
    .get_mirror_y = nullptr,
    .set_gap = nullptr,
    .get_gap_x = nullptr,
    .get_gap_y = nullptr,
    .invert_color = nullptr,
    .disp_on_off = gdeq031t10_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = gdeq031t10_get_color_format,
    .get_resolution_x = gdeq031t10_get_resolution_x,
    .get_resolution_y = gdeq031t10_get_resolution_y,
    .get_frame_buffer = gdeq031t10_get_frame_buffer,
    .get_frame_buffer_count = gdeq031t10_get_frame_buffer_count,
    .get_backlight = nullptr,
    .has_capability = nullptr,
};

// region Driver lifecycle

static void free_internal(Gdeq031t10Internal* internal) {
    if (internal->spi_device != nullptr) {
        spi_bus_remove_device(internal->spi_device);
    }
    if (internal->dc != nullptr) {
        gpio_descriptor_release(internal->dc);
    }
    if (internal->reset != nullptr) {
        gpio_descriptor_release(internal->reset);
    }
    if (internal->busy != nullptr) {
        gpio_descriptor_release(internal->busy);
    }
    if (internal->panel_mutex != nullptr) {
        vSemaphoreDelete(internal->panel_mutex);
    }
    free(internal->shadow_framebuffer);
    free(internal->region_buffer);
    free(internal);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    struct GpioPinSpec cs_pin;
    if (esp32_spi_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Gdeq031t10Internal*>(calloc(1, sizeof(Gdeq031t10Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->dc = gpio_descriptor_acquire(
        config->pin_dc.gpio_controller,
        config->pin_dc.pin,
        config->pin_dc.flags | GPIO_FLAG_DIRECTION_OUTPUT,
        GPIO_OWNER_GPIO
    );

    if (config->pin_reset.gpio_controller != nullptr) {
        internal->reset = gpio_descriptor_acquire(
            config->pin_reset.gpio_controller,
            config->pin_reset.pin,
            config->pin_reset.flags | GPIO_FLAG_DIRECTION_OUTPUT,
            GPIO_OWNER_GPIO
        );
    } else {
        internal->reset = nullptr;
    }

    internal->busy = gpio_descriptor_acquire(
        config->pin_busy.gpio_controller,
        config->pin_busy.pin,
        config->pin_busy.flags | GPIO_FLAG_DIRECTION_INPUT | GPIO_FLAG_ACTIVE_LOW,
        GPIO_OWNER_GPIO
    );

    internal->panel_mutex = xSemaphoreCreateMutex();
    internal->shadow_framebuffer = static_cast<uint8_t*>(malloc(FRAMEBUFFER_SIZE));
    internal->region_buffer = static_cast<uint8_t*>(malloc(FRAMEBUFFER_SIZE));
    if (
        internal->dc == nullptr ||
        internal->busy == nullptr ||
        internal->panel_mutex == nullptr ||
        internal->shadow_framebuffer == nullptr ||
        internal->region_buffer == nullptr
        // Note: reset pin is not checked as it is optional
    ) {
        LOG_E(TAG, "Failed to acquire GPIO pins or allocate buffers");
        free_internal(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    gpio_descriptor_set_flags(internal->dc, GPIO_FLAG_DIRECTION_OUTPUT);
    gpio_descriptor_set_flags(internal->busy, GPIO_FLAG_DIRECTION_INPUT);
    if (internal->reset != nullptr) {
        gpio_descriptor_set_flags(internal->reset, GPIO_FLAG_DIRECTION_OUTPUT);
    }

    spi_device_interface_config_t device_config = {};
    device_config.mode = 0;
    device_config.clock_speed_hz = config->clock_speed_hz;
    device_config.spics_io_num = cs_pin.gpio_controller == nullptr ? -1 : static_cast<int>(cs_pin.pin);
    device_config.queue_size = 1;

    if (spi_bus_add_device(spi_config->host, &device_config, &internal->spi_device) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device");
        free_internal(internal);
        return ERROR_RESOURCE;
    }

    // 0xFF matches an all-white render (bit 1 = white), consistent with the panel's blank
    // power-up state and with the forced first full refresh below.
    std::memset(internal->shadow_framebuffer, 0xFF, FRAMEBUFFER_SIZE);

    if (!init_full(internal, config->mirror_180)) {
        free_internal(internal);
        return ERROR_RESOURCE;
    }

    internal->display_on = true;
    internal->force_full_refresh = true;

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));

    xSemaphoreTake(internal->panel_mutex, portMAX_DELAY);
    if (internal->display_on) {
        // Same sequence as disp_on_off(false): leave the panel in deep sleep.
        if (internal->panel_power_on) {
            panel_power_off(internal);
        }
        delay_millis(100);
        write_command(internal, CMD_DEEP_SLEEP);
        write_data_byte(internal, DEEP_SLEEP_CHECK_CODE);
        internal->display_on = false;
    }
    xSemaphoreGive(internal->panel_mutex);

    free_internal(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

Driver gdeq031t10_driver = {
    .name = "gdeq031t10",
    .compatible = (const char*[]) { "gooddisplay,gdeq031t10", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &gdeq031t10_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &gdeq031t10_module,
    .internal = nullptr
};

}