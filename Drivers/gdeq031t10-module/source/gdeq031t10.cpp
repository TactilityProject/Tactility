// SPDX-License-Identifier: Apache-2.0
#include <drivers/gdeq031t10.h>
#include <gdeq031t10_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#define TAG "GDEQ031T10"
#define GET_CONFIG(device) (static_cast<const Gdeq031t10Config*>((device)->config))

// UC8253-family commands, ported from Xinyuan-LilyGO/T-Deck-MAX's
// Display_EPD_W21.cpp reference driver. No SWRESET opcode exists in this family - a full
// register reset can only be done by toggling the hardware reset line (see hardware_reset()).
namespace {
constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_POWER_ON_OFF = 0x02; // shared opcode: power on when followed by 0x04, power off as standalone 0x02
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DATA_START_OLD = 0x10;
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_DATA_START_NEW = 0x13;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_PARTIAL_WINDOW = 0x90;
constexpr uint8_t CMD_PARTIAL_IN = 0x91;
constexpr uint8_t CMD_PARTIAL_OUT = 0x92;
constexpr uint8_t CMD_FAST_MODE_ENABLE = 0xE0;
constexpr uint8_t CMD_FAST_MODE_TIMING = 0xE5;

constexpr uint8_t DEEP_SLEEP_CHECK_CODE = 0xA5;

// Fixed panel geometry (this driver targets one specific panel, not a controller family with
// varying sizes) - matches the deprecated HAL's Gdeq031t10Display.
constexpr uint16_t WIDTH = 240;
constexpr uint16_t HEIGHT = 320;
constexpr size_t BYTES_PER_ROW = WIDTH / 8;
constexpr size_t FRAMEBUFFER_SIZE = BYTES_PER_ROW * HEIGHT; // 1 bpp packed
constexpr uint8_t MAX_PARTIAL_REFRESHES = 8;
}

struct Gdeq031t10Internal {
    spi_device_handle_t spi_handle;
    gpio_num_t pin_dc;
    gpio_num_t pin_reset; // GPIO_NUM_NC when the board has no reset line
    gpio_num_t pin_busy;
    bool mirror_180;
    uint8_t default_refresh_mode; // GDEQ031T10_REFRESH_*, used for automatic (non-explicit) refreshes

    // Mirrors what the panel currently holds, required by the controller's "old data" + "new
    // data" double-buffered refresh protocol. Panel polarity (inverted vs. the incoming LVGL
    // MONOCHROME bitmap).
    uint8_t* shadow_framebuffer;
    // Scratch buffer used to gather a windowed region's bytes for one SPI write.
    uint8_t* region_buffer;

    bool powered;
    bool panel_power_on; // charge pump state
    int panel_mode; // -1 = unknown (registers need a fresh full bring-up), else GDEQ031T10_REFRESH_*
    // Number of windowed partial refreshes since the last full refresh. A full refresh is forced
    // once this reaches MAX_PARTIAL_REFRESHES to clear the ghosting that partial updates accumulate.
    uint8_t partial_refresh_count;
    // Union bounding box (byte-column/row space) of all windowed partial refreshes since the last
    // ghost-clear; decides between a localized scrub and a full-screen refresh once the
    // partial-refresh gate expires.
    bool ghost_area_valid;
    int ghost_first_col;
    int ghost_last_col;
    int ghost_first_row;
    int ghost_last_row;
    // Forces the next draw_bitmap() call to use a full-quality refresh (set at boot/power-on).
    bool force_full_refresh;
};

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// region SPI transport

static bool spi_write_command(Gdeq031t10Internal* internal, uint8_t command) {
    gpio_set_level(internal->pin_dc, 0);
    spi_transaction_t transaction = {};
    transaction.length = 8;
    transaction.tx_buffer = &command;
    if (spi_device_polling_transmit(internal->spi_handle, &transaction) != ESP_OK) {
        LOG_E(TAG, "SPI command transfer failed");
        return false;
    }
    return true;
}

static bool spi_write_data(Gdeq031t10Internal* internal, const uint8_t* data, size_t length) {
    gpio_set_level(internal->pin_dc, 1);
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    transaction.tx_buffer = data;
    if (spi_device_polling_transmit(internal->spi_handle, &transaction) != ESP_OK) {
        LOG_E(TAG, "SPI data transfer failed");
        return false;
    }
    return true;
}

static bool spi_write_data(Gdeq031t10Internal* internal, uint8_t data) {
    return spi_write_data(internal, &data, 1);
}

static bool wait_while_busy(const Gdeq031t10Internal* internal) {
    // BUSY pin reads high when the controller is idle/ready.
    constexpr TickType_t timeout = pdMS_TO_TICKS(5000);
    const TickType_t start = xTaskGetTickCount();
    while (gpio_get_level(internal->pin_busy) != 0) {
        if (xTaskGetTickCount() - start > timeout) {
            LOG_E(TAG, "Timed out waiting for panel BUSY");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return true;
}

static void hardware_reset(const Gdeq031t10Internal* internal) {
    if (internal->pin_reset == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(internal->pin_reset, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(internal->pin_reset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// endregion

// region Bring-up / mode switching

static bool write_bringup_full(Gdeq031t10Internal* internal) {
    bool ok = spi_write_command(internal, CMD_PANEL_SETTING);
    ok = ok && spi_write_data(internal, internal->mirror_180 ? 0x13 : 0x1F);
    ok = ok && spi_write_command(internal, CMD_POWER_ON);
    ok = ok && wait_while_busy(internal);
    if (!ok) {
        LOG_E(TAG, "Full bring-up failed");
        return false;
    }
    internal->panel_mode = GDEQ031T10_REFRESH_FULL;
    internal->panel_power_on = true;
    return true;
}

static bool init_full(Gdeq031t10Internal* internal) {
    hardware_reset(internal);
    return write_bringup_full(internal);
}

static bool init_fast(Gdeq031t10Internal* internal) {
    if (!init_full(internal)) {
        return false;
    }
    bool ok = spi_write_command(internal, CMD_FAST_MODE_ENABLE);
    ok = ok && spi_write_data(internal, 0x02);
    ok = ok && spi_write_command(internal, CMD_FAST_MODE_TIMING);
    ok = ok && spi_write_data(internal, 0x5A); // ~1.0s
    if (!ok) {
        LOG_E(TAG, "Fast mode init failed");
        return false;
    }
    internal->panel_mode = GDEQ031T10_REFRESH_FAST;
    return true;
}

static bool init_slow(Gdeq031t10Internal* internal) {
    if (!init_full(internal)) {
        return false;
    }
    bool ok = spi_write_command(internal, CMD_FAST_MODE_ENABLE);
    ok = ok && spi_write_data(internal, 0x02);
    ok = ok && spi_write_command(internal, CMD_FAST_MODE_TIMING);
    ok = ok && spi_write_data(internal, 0x6E); // ~1.5s
    if (!ok) {
        LOG_E(TAG, "Slow mode init failed");
        return false;
    }
    internal->panel_mode = GDEQ031T10_REFRESH_SLOW;
    return true;
}

static bool init_partial(Gdeq031t10Internal* internal) {
    if (!init_full(internal)) {
        return false;
    }
    bool ok = spi_write_command(internal, CMD_FAST_MODE_ENABLE);
    ok = ok && spi_write_data(internal, 0x02);
    ok = ok && spi_write_command(internal, CMD_FAST_MODE_TIMING);
    ok = ok && spi_write_data(internal, 0x79);
    ok = ok && spi_write_command(internal, CMD_VCOM_DATA_INTERVAL);
    ok = ok && spi_write_data(internal, 0xD7);
    if (!ok) {
        LOG_E(TAG, "Partial mode init failed");
        return false;
    }
    internal->panel_mode = GDEQ031T10_REFRESH_PARTIAL;
    return true;
}

static bool ensure_panel_ready(Gdeq031t10Internal* internal, uint8_t mode) {
    if (internal->panel_mode != static_cast<int>(mode)) {
        // A mode change needs the full init path: it starts with a (possible) hardware reset,
        // which restores register defaults (required when leaving partial mode, whose
        // VCOM/data-interval setting would otherwise linger).
        switch (mode) {
            case GDEQ031T10_REFRESH_FULL: return init_full(internal);
            case GDEQ031T10_REFRESH_FAST: return init_fast(internal);
            case GDEQ031T10_REFRESH_SLOW: return init_slow(internal);
            case GDEQ031T10_REFRESH_PARTIAL: return init_partial(internal);
            default: return false;
        }
    } else if (!internal->panel_power_on) {
        // Registers still hold the mode; only the charge pump was idled.
        if (!spi_write_command(internal, CMD_POWER_ON) || !wait_while_busy(internal)) {
            LOG_E(TAG, "Panel did not become ready after power-on");
            return false;
        }
        internal->panel_power_on = true;
    }
    return true;
}

static bool power_off(Gdeq031t10Internal* internal) {
    // Command the panel off regardless of whether BUSY confirms it: retrying forever here would
    // just as likely hang, and a stuck-BUSY panel is already unusable either way.
    bool ok = spi_write_command(internal, CMD_POWER_ON_OFF); // 0x02 standalone = power off
    if (!ok || !wait_while_busy(internal)) {
        LOG_E(TAG, "Panel did not confirm power-off");
        ok = false;
    }
    internal->panel_power_on = false;
    return ok;
}

// Drops the panel to its lowest-power reachable state. Deep sleep can only be woken back up by a
// hardware reset, so it's only used when a reset pin is actually wired - boards without one (see
// pin-reset's binding description) stay in the charge-pump-off standby that ensure_panel_ready()
// can recover from with just a CMD_POWER_ON.
static void enter_standby(Gdeq031t10Internal* internal) {
    if (internal->panel_power_on) {
        power_off(internal);
    }
    if (internal->pin_reset != GPIO_NUM_NC) {
        vTaskDelay(pdMS_TO_TICKS(100));
        spi_write_command(internal, CMD_DEEP_SLEEP);
        spi_write_data(internal, DEEP_SLEEP_CHECK_CODE);
        internal->panel_mode = -1; // deep sleep restores register defaults; needs a fresh bring-up
    }
}

// endregion

// region Refresh

static void refresh_full(Gdeq031t10Internal* internal, uint8_t mode, const uint8_t* render_bitmap) {
    if (!ensure_panel_ready(internal, mode)) {
        LOG_E(TAG, "Skipping full refresh: panel not ready");
        return;
    }

    // shadow_framebuffer keeps the panel-polarity copy of the last frame for the controller's
    // old/new differential refresh; the incoming MONOCHROME bitmap's polarity is inverted.
    if (!spi_write_command(internal, CMD_DATA_START_OLD) || !spi_write_data(internal, internal->shadow_framebuffer, FRAMEBUFFER_SIZE)) {
        LOG_E(TAG, "Failed to send old frame data");
        return;
    }

    for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
        internal->shadow_framebuffer[i] = static_cast<uint8_t>(~render_bitmap[i]);
    }
    if (!spi_write_command(internal, CMD_DATA_START_NEW) || !spi_write_data(internal, internal->shadow_framebuffer, FRAMEBUFFER_SIZE)) {
        LOG_E(TAG, "Failed to send new frame data");
        return;
    }

    if (!spi_write_command(internal, CMD_DISPLAY_REFRESH)) {
        LOG_E(TAG, "Failed to trigger display refresh");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1)); // datasheet requires >=200us settle before polling BUSY
    if (!wait_while_busy(internal)) {
        LOG_E(TAG, "Full refresh did not complete");
    }
}

static void refresh_window(Gdeq031t10Internal* internal, int first_byte_col, int last_byte_col, int first_row, int last_row, bool scrub_ghosts, const uint8_t* render_bitmap) {
    // Partial LUT (fast waveform via temperature force) on a sub-region only. The RAM window must
    // be byte-aligned in X, which it already is (byte columns).
    if (!ensure_panel_ready(internal, GDEQ031T10_REFRESH_PARTIAL)) {
        LOG_E(TAG, "Skipping window refresh: panel not ready");
        return;
    }

    const int width_bytes = last_byte_col - first_byte_col + 1;

    const auto x = static_cast<uint16_t>(first_byte_col * 8);
    const auto xe = static_cast<uint16_t>(last_byte_col * 8 + 7);
    const auto y = static_cast<uint16_t>(first_row);
    const auto ye = static_cast<uint16_t>(last_row);

    // Set the partial RAM window (GxEPD2 GDEQ031T10 sequence).
    bool ok = spi_write_command(internal, CMD_PARTIAL_IN);
    ok = ok && spi_write_command(internal, CMD_PARTIAL_WINDOW);
    ok = ok && spi_write_data(internal, static_cast<uint8_t>(x));
    ok = ok && spi_write_data(internal, static_cast<uint8_t>(xe));
    ok = ok && spi_write_data(internal, static_cast<uint8_t>(y >> 8));
    ok = ok && spi_write_data(internal, static_cast<uint8_t>(y & 0xFF));
    ok = ok && spi_write_data(internal, static_cast<uint8_t>(ye >> 8));
    ok = ok && spi_write_data(internal, static_cast<uint8_t>(ye & 0xFF));
    ok = ok && spi_write_data(internal, 0x01);
    if (!ok) {
        LOG_E(TAG, "Failed to set partial refresh window");
        spi_write_command(internal, CMD_PARTIAL_OUT); // best-effort: leave partial-window mode
        return;
    }

    // Old region: normally the region's current panel contents (shadow), gathered contiguously.
    // For a ghost scrub, feed the complement of the new data instead: the controller then sees
    // every pixel as changed and drives each one through a transition, clearing accumulated
    // partial-refresh ghosting in this window only.
    size_t n = 0;
    for (int row = first_row; row <= last_row; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW + first_byte_col;
        if (scrub_ghosts) {
            // New panel data is ~render_bitmap, so its complement is render_bitmap.
            std::memcpy(&internal->region_buffer[n], &render_bitmap[base], width_bytes);
        } else {
            std::memcpy(&internal->region_buffer[n], &internal->shadow_framebuffer[base], width_bytes);
        }
        n += width_bytes;
    }
    if (!spi_write_command(internal, CMD_DATA_START_OLD) || !spi_write_data(internal, internal->region_buffer, n)) {
        LOG_E(TAG, "Failed to send old window data");
        spi_write_command(internal, CMD_PARTIAL_OUT);
        return;
    }

    // New region: inverted render, and update the shadow for this region as we go.
    n = 0;
    for (int row = first_row; row <= last_row; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW + first_byte_col;
        for (int c = 0; c < width_bytes; c++) {
            const auto value = static_cast<uint8_t>(~render_bitmap[base + c]);
            internal->region_buffer[n++] = value;
            internal->shadow_framebuffer[base + c] = value;
        }
    }
    if (!spi_write_command(internal, CMD_DATA_START_NEW) || !spi_write_data(internal, internal->region_buffer, n)) {
        LOG_E(TAG, "Failed to send new window data");
        spi_write_command(internal, CMD_PARTIAL_OUT);
        return;
    }

    if (spi_write_command(internal, CMD_DISPLAY_REFRESH)) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if (!wait_while_busy(internal)) {
            LOG_E(TAG, "Window refresh did not complete");
        }
    } else {
        LOG_E(TAG, "Failed to trigger window refresh");
    }
    spi_write_command(internal, CMD_PARTIAL_OUT);
}

// endregion

// region DisplayApi

static error_t gdeq031t10_reset(Device* device) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    hardware_reset(internal);
    return ERROR_NONE;
}

static error_t gdeq031t10_init(Device* device) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    return write_bringup_full(internal) ? ERROR_NONE : ERROR_RESOURCE;
}

// LVGL only ever calls this with the full frame: DISPLAY_COLOR_FORMAT_MONOCHROME forces
// LV_DISPLAY_RENDER_MODE_FULL in the generic kernel LVGL bridge (lvgl_display.c), and FULL mode
// only presents (calls draw_bitmap) once per render cycle, with the complete 0,0..hres,vres rect.
// DisplayApi has no async completion callback, so this runs the refresh synchronously - unlike
// the deprecated HAL's Gdeq031t10Display, there's no dedicated background refresh task here.
static error_t gdeq031t10_draw_bitmap(Device* device, int32_t /*x_start*/, int32_t /*y_start*/, int32_t /*x_end*/, int32_t /*y_end*/, const void* color_data) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    const auto* render_bitmap = static_cast<const uint8_t*>(color_data);

    // Find the bounding box of bytes that differ from what the panel holds. The panel stores the
    // inverted (shadow) polarity, so compare against ~render.
    int first_col = BYTES_PER_ROW, last_col = -1, first_row = HEIGHT, last_row = -1;
    for (int row = 0; row < HEIGHT; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW;
        for (size_t col = 0; col < BYTES_PER_ROW; col++) {
            if (static_cast<uint8_t>(~render_bitmap[base + col]) != internal->shadow_framebuffer[base + col]) {
                if (static_cast<int>(col) < first_col) first_col = static_cast<int>(col);
                if (static_cast<int>(col) > last_col) last_col = static_cast<int>(col);
                if (row < first_row) first_row = row;
                if (row > last_row) last_row = row;
            }
        }
    }

    const bool nothing_changed = (last_col < 0);
    if (nothing_changed && !internal->force_full_refresh) {
        return ERROR_NONE; // panel already shows this frame; don't refresh needlessly
    }

    // A change covering most of the screen or an explicit request warrants a full refresh;
    // otherwise refresh just the box.
    const int changed_rows = nothing_changed ? 0 : (last_row - first_row + 1);
    const bool large_change = changed_rows > (HEIGHT * 3 / 4);

    if (!internal->powered) {
        // The display was turned off while this frame arrived; the shadow mismatch makes the
        // frame redraw the next time draw_bitmap() is called after power-on.
        return ERROR_NONE;
    }

    if (internal->force_full_refresh || large_change || nothing_changed) {
        // Explicit requests get the best-quality LUT; automatic ghost-clears use the
        // devicetree-configured (typically faster) mode.
        refresh_full(internal, internal->force_full_refresh ? GDEQ031T10_REFRESH_FULL : internal->default_refresh_mode, render_bitmap);
        internal->force_full_refresh = false;
        internal->partial_refresh_count = 0;
        internal->ghost_area_valid = false;
        return ERROR_NONE;
    }

    // Grow the ghost union with this change so the gate sees where partial updates have
    // accumulated since the last clear.
    if (!internal->ghost_area_valid) {
        internal->ghost_first_col = first_col;
        internal->ghost_last_col = last_col;
        internal->ghost_first_row = first_row;
        internal->ghost_last_row = last_row;
        internal->ghost_area_valid = true;
    } else {
        internal->ghost_first_col = std::min(internal->ghost_first_col, first_col);
        internal->ghost_last_col = std::max(internal->ghost_last_col, last_col);
        internal->ghost_first_row = std::min(internal->ghost_first_row, first_row);
        internal->ghost_last_row = std::max(internal->ghost_last_row, last_row);
    }

    if (internal->partial_refresh_count >= MAX_PARTIAL_REFRESHES) {
        // Ghost-clear gate. If the accumulated churn is confined to a small region (a blinking
        // cursor, a line being typed into), scrub just that window - a full-screen flash there is
        // needless and distracting. Only widespread churn earns a whole-panel refresh.
        const int union_width = (internal->ghost_last_col - internal->ghost_first_col + 1) * 8;
        const int union_height = internal->ghost_last_row - internal->ghost_first_row + 1;
        const bool union_is_large = union_width * union_height * 4 >= WIDTH * HEIGHT; // >= 25% of the panel
        if (union_is_large) {
            refresh_full(internal, internal->default_refresh_mode, render_bitmap);
        } else {
            refresh_window(internal, internal->ghost_first_col, internal->ghost_last_col, internal->ghost_first_row, internal->ghost_last_row, true, render_bitmap);
        }
        internal->partial_refresh_count = 0;
        internal->ghost_area_valid = false;
    } else {
        refresh_window(internal, first_col, last_col, first_row, last_row, false, render_bitmap);
        internal->partial_refresh_count++;
    }
    return ERROR_NONE;
}

static error_t gdeq031t10_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));
    if (on_off == internal->powered) {
        return ERROR_NONE;
    }

    if (on_off) {
        // A hardware reset (when available) also wakes the panel from deep sleep.
        if (!init_full(internal)) {
            LOG_E(TAG, "Failed to wake panel");
            return ERROR_RESOURCE;
        }
        // Content may be stale (fresh registers, or content sent while powered off never made it
        // to the panel) - force the next draw_bitmap() to redraw unconditionally.
        internal->force_full_refresh = true;
    } else {
        enter_standby(internal);
    }

    internal->powered = on_off;
    return ERROR_NONE;
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
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME | DISPLAY_CAPABILITY_SLOW_REFRESH,
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

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    GpioPinSpec cs_pin;
    if (esp32_spi_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Gdeq031t10Internal*>(malloc(sizeof(Gdeq031t10Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    std::memset(internal, 0, sizeof(Gdeq031t10Internal));

    internal->pin_dc = static_cast<gpio_num_t>(pin_or_unused(config->pin_dc));
    internal->pin_busy = static_cast<gpio_num_t>(pin_or_unused(config->pin_busy));
    internal->pin_reset = static_cast<gpio_num_t>(pin_or_unused(config->pin_reset));
    internal->mirror_180 = config->mirror_180;
    internal->default_refresh_mode = config->refresh_mode;
    internal->panel_mode = -1;

    gpio_config_t dc_config = {
        .pin_bit_mask = 1ULL << internal->pin_dc,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&dc_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure DC pin");
        free(internal);
        return ERROR_RESOURCE;
    }

    gpio_config_t busy_config = {
        .pin_bit_mask = 1ULL << internal->pin_busy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&busy_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure BUSY pin");
        free(internal);
        return ERROR_RESOURCE;
    }

    if (internal->pin_reset != GPIO_NUM_NC) {
        gpio_config_t reset_config = {
            .pin_bit_mask = 1ULL << internal->pin_reset,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&reset_config) != ESP_OK) {
            LOG_E(TAG, "Failed to configure reset pin");
            free(internal);
            return ERROR_RESOURCE;
        }
    }

    spi_device_interface_config_t device_config = {
        .mode = 0,
        .clock_speed_hz = static_cast<int>(config->clock_speed_hz),
        .spics_io_num = pin_or_unused(cs_pin),
        .flags = 0,
        .queue_size = 1,
    };

    if (spi_bus_add_device(spi_config->host, &device_config, &internal->spi_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device");
        free(internal);
        return ERROR_RESOURCE;
    }

    internal->shadow_framebuffer = static_cast<uint8_t*>(malloc(FRAMEBUFFER_SIZE));
    internal->region_buffer = static_cast<uint8_t*>(malloc(FRAMEBUFFER_SIZE));
    if (internal->shadow_framebuffer == nullptr || internal->region_buffer == nullptr) {
        free(internal->shadow_framebuffer);
        free(internal->region_buffer);
        spi_bus_remove_device(internal->spi_handle);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }
    std::memset(internal->shadow_framebuffer, 0xFF, FRAMEBUFFER_SIZE);

    if (!init_full(internal)) {
        LOG_E(TAG, "Panel bring-up failed");
        free(internal->shadow_framebuffer);
        free(internal->region_buffer);
        spi_bus_remove_device(internal->spi_handle);
        free(internal);
        return ERROR_RESOURCE;
    }

    internal->powered = true;
    internal->force_full_refresh = true;

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Gdeq031t10Internal*>(device_get_driver_data(device));

    if (internal->powered) {
        enter_standby(internal);
    }

    if (spi_bus_remove_device(internal->spi_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to remove SPI device");
        return ERROR_RESOURCE;
    }

    free(internal->shadow_framebuffer);
    free(internal->region_buffer);
    free(internal);
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
