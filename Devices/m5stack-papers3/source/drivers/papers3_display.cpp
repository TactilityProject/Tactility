// SPDX-License-Identifier: Apache-2.0
#include "papers3_display.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <tactility/time.h>

#include "epd_board_m5papers3.h"

#include <epdiy.h>

#include <cstdlib>
#include <cstring>

#define TAG "Papers3Display"
#define GET_CONFIG(device) (static_cast<const Papers3DisplayConfig*>((device)->config))

// Fast partial updates are always MODE_DU (strict black/white); config->quality_draw_mode is
// only used for the periodic full-quality pass.
static constexpr EpdDrawMode FAST_DRAW_MODE = MODE_DU;

// A partial update covering at least this fraction of the panel is a full-screen content change
// (e.g. an app switch rebuilding the whole window, see lvgl.md) rather than a small widget
// redraw, and is promoted to a quality refresh immediately.
static constexpr float FULL_AREA_QUALITY_THRESHOLD = 0.6f;

// Bounds worst-case ghost accumulation during sustained fast-mode interaction (e.g. scrolling),
// regardless of idle time. LVGL's PARTIAL-mode draw buffer covers vres/10 rows (see
// lvgl-module/source/devices/devices.cpp's buffer_height), so a single full-screen redraw is
// already ~10 tiles. This must clear a full sweep comfortably, or a normal full-screen redraw
// gets promoted to slow GC16 partway through.
static constexpr uint32_t QUALITY_REFRESH_PARTIAL_COUNT = 20;

// LVGL's PARTIAL render mode flushes one draw_bitmap() call per still-unjoined dirty rect, so
// one visual refresh is usually several back-to-back calls, not one; this holds quality mode
// across a sibling rect's near-zero gap so they don't end up on inconsistent modes. Must stay
// well under a GC16 draw's own duration (400ms+), or it also bridges the much larger gap between
// separate real frames and pins a whole multi-frame interaction to GC16.
static constexpr uint32_t QUALITY_HOLD_MS = 50;

// Caps how long a single hold session can keep re-extending itself (see
// commit_quality_mode_decision()). A real multi-rect LVGL redraw finishes well within this; a
// caller whose own triggers (is_full_screen_change/partial_count_exceeded) keep firing faster
// than QUALITY_HOLD_MS apart, such as GraphicsDemo's tight banded draw loop, would otherwise keep
// re-extending the hold indefinitely once one of those triggers happened to line up with an
// active hold window.
static constexpr uint32_t QUALITY_HOLD_SESSION_MAX_MS = 200;

// 4x4 ordered (Bayer) dither thresholds, spread evenly across a 0-15 nibble range.
static constexpr uint8_t BAYER_4X4[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 },
};

// Dithers an 8-bit luminance sample (0x00=black..0xFF=white) down to a 4-bit nibble
// (0x0=black..0xF=white, matching EPDiy's MODE_PACKING_2PPB), spreading the rounding error
// spatially instead of truncating every pixel the same way. This is what turns flat/banded
// output into something that reads as smooth grayscale.
static inline uint8_t dither_to_nibble(uint8_t luminance, int32_t x, int32_t y) {
    // BAYER_4X4 is 0-15; scaled by 17 it spans a full 0-255 quantization step (one increment of
    // luminance*15), so the dither bias is actually comparable to the rounding it perturbs
    // instead of a few percent of one step.
    const uint32_t threshold = BAYER_4X4[y & 3][x & 3] * 17U;
    const uint32_t level = (static_cast<uint32_t>(luminance) * 15U + threshold) / 255U;
    return static_cast<uint8_t>(level > 15U ? 15U : level);
}

// Binary variant for MODE_DU, which only supports pure black/white (see epdiy.h). Dithering
// still applies so a partial-update area doesn't look coarser than the quality pass that
// preceded it.
static inline uint8_t dither_to_bw_nibble(uint8_t luminance, int32_t x, int32_t y) {
    // *16 (not 17) keeps the max threshold at 240, strictly below 255. Otherwise luminance 0xFF
    // (pure white) would tie the top Bayer cell's threshold and the strict ">" would misclassify
    // it as black.
    const uint32_t threshold = BAYER_4X4[y & 3][x & 3] * 16U;
    return luminance > threshold ? 0xF : 0x0;
}

extern "C" {

extern Module m5stack_papers3_module;

// epd_hl_init() has no matching deinit and sets an internal already_initialized flag, so the
// highlevel state must persist across stop()/start() cycles and be reused rather than recreated.
static bool s_hl_initialized = false;
static EpdiyHighlevelState s_hl_state = {};

struct Papers3DisplayInternal {
    EpdiyHighlevelState hl_state;
    uint8_t* framebuffer;
    bool powered;
    uint32_t panel_pixel_count;
    // Fast (MODE_DU) partial updates since the last quality refresh; see
    // QUALITY_REFRESH_PARTIAL_COUNT.
    uint32_t partial_count_since_quality;
    // While get_ticks() < this, every draw_bitmap() call uses quality mode regardless of the
    // other triggers; see QUALITY_HOLD_MS.
    TickType_t quality_hold_until_tick;
    // get_ticks() when the current hold session first armed; see QUALITY_HOLD_SESSION_MAX_MS.
    TickType_t quality_hold_session_start_tick;
};

static void power_on(Papers3DisplayInternal* internal) {
    if (!internal->powered) {
        epd_poweron();
        internal->powered = true;
    }
}

// region DisplayApi

static error_t papers3_display_reset(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    // EPD has no discrete reset pin/sequence the way SPI TFT panels do. epd_init() (in start())
    // already performs the real one-time hardware bring-up, so a power-cycle is the closest
    // equivalent available at runtime.
    epd_poweroff();
    internal->powered = false;
    power_on(internal);
    return ERROR_NONE;
}

/**
 * Registered as the init() callback, but the current boot path never calls the public
 * display_init() wrapper. start() does this work directly instead. Kept as a harmless no-op
 * for forward compatibility.
 * \see start
 */
static error_t papers3_display_init(Device*) {
    return ERROR_NONE;
}

/**
 * White-fill plus a single GC16 redraw. Wipes content an app drew directly via draw_bitmap(),
 * skipping epd_fullclear()'s 3-cycle hardware flash, which stays reserved for the boot-priming
 * clear.
 * \see start
 */
static error_t papers3_display_clear(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    power_on(internal);
    epd_hl_set_all_white(&internal->hl_state);
    auto result = epd_hl_update_screen(&internal->hl_state, MODE_GC16, config->temperature_celsius);
    if (result == EPD_DRAW_SUCCESS) {
        internal->partial_count_since_quality = 0;
        internal->quality_hold_until_tick = 0;
    }
    return result == EPD_DRAW_SUCCESS ? ERROR_NONE : ERROR_RESOURCE;
}

/**
 * A GC16 pass over whatever's already in front_fb, without a preceding white-fill, so it clears
 * ghosting without visibly changing on-screen content. MODE_DU alone never fully clears prior
 * ghosting even when it draws the correct pixels.
 *
 * epd_hl_update_area()/epd_hl_update_screen() diff front_fb against back_fb and skip any pixel
 * where the two already match, regardless of draw mode (see epd_difference_image_base in epdiy's
 * render.c). A ghosted region whose logical content hasn't changed since it was last
 * committed to back_fb is exactly such a match, so a plain GC16 pass silently skips physically
 * re-driving it. Bitwise-inverting back_fb guarantees a mismatch on every byte (a byte can never
 * equal its own complement), forcing the diff to treat the whole panel as dirty; back_fb resyncs
 * to front_fb afterward via the diff's own post-draw copy.
 */
static error_t papers3_display_refresh(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    power_on(internal);
    const size_t fb_size = static_cast<size_t>(epd_width()) / 2 * epd_height();
    uint8_t* back_fb = internal->hl_state.back_fb;
    for (size_t i = 0; i < fb_size; i++) {
        back_fb[i] = static_cast<uint8_t>(~back_fb[i]);
    }
    auto result = epd_hl_update_screen(&internal->hl_state, MODE_GC16, config->temperature_celsius);
    if (result == EPD_DRAW_SUCCESS) {
        internal->partial_count_since_quality = 0;
        internal->quality_hold_until_tick = 0;
    }
    return result == EPD_DRAW_SUCCESS ? ERROR_NONE : ERROR_RESOURCE;
}

// Decides whether this update should be a full-quality (config->quality_draw_mode) refresh or
// a fast MODE_DU one. Read-only; see commit_quality_mode_decision() for the state this decision
// leads to. *out_within_hold reports whether the hold trigger specifically fired, since
// commit_quality_mode_decision() treats that case differently from the others.
static bool should_use_quality_mode(Papers3DisplayInternal* internal, int32_t width, int32_t height, bool* out_within_hold) {
    const TickType_t now = get_ticks();

    const uint32_t area = static_cast<uint32_t>(width) * static_cast<uint32_t>(height);
    const bool is_full_screen_change = area >= static_cast<uint32_t>(
        static_cast<float>(internal->panel_pixel_count) * FULL_AREA_QUALITY_THRESHOLD
    );
    const bool partial_count_exceeded = internal->partial_count_since_quality >= QUALITY_REFRESH_PARTIAL_COUNT;
    const bool within_hold = now < internal->quality_hold_until_tick;
    *out_within_hold = within_hold;

    return is_full_screen_change || partial_count_exceeded || within_hold;
}

// Applies should_use_quality_mode()'s decision, but only commits the quality-mode reset once the
// draw actually succeeded. A failed quality refresh must not make a still-ghosting panel look
// freshly cleaned to every trigger above. A failed fast update still counts toward the partial
// count, since it was still MODE_DU content, not a clean slate.
//
// quality_hold_until_tick exists to bridge the near-zero gap between sibling dirty rects of the
// SAME LVGL redraw (see QUALITY_HOLD_MS's comment). A GC16 draw's own duration (400ms+) already
// exceeds QUALITY_HOLD_MS, so unconditionally re-arming a fresh window on every successful quality
// draw let a caller whose own triggers (is_full_screen_change/partial_count_exceeded) keep firing
// faster than QUALITY_HOLD_MS apart, such as GraphicsDemo's tight banded draw loop, perpetuate
// quality mode forever, since each such draw's re-arm was still live by the time the next one
// landed. quality_hold_session_start_tick bounds this: a hold session may keep extending itself
// to bridge consecutive sibling rects, but never past QUALITY_HOLD_SESSION_MAX_MS from when it
// first armed, regardless of how it keeps getting triggered.
static void commit_quality_mode_decision(Papers3DisplayInternal* internal, bool used_quality, bool draw_succeeded, bool was_within_hold) {
    if (used_quality) {
        if (draw_succeeded) {
            internal->partial_count_since_quality = 0;
            const TickType_t now = get_ticks();
            if (!was_within_hold) {
                internal->quality_hold_session_start_tick = now;
            }
            const TickType_t session_elapsed = now - internal->quality_hold_session_start_tick;
            if (session_elapsed < millis_to_ticks(QUALITY_HOLD_SESSION_MAX_MS)) {
                internal->quality_hold_until_tick = now + millis_to_ticks(QUALITY_HOLD_MS);
            }
        }
    } else {
        internal->partial_count_since_quality++;
    }
}

// Reports GRAYSCALE8 (not MONOCHROME) so LVGL uses partial/tile updates instead of forcing
// full-frame, since the bridge hardcodes full-frame for MONOCHROME/I1 regardless of capability
// flags. So draw_bitmap is called once per changed tile, not necessarily the whole panel.
static error_t papers3_display_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    const int32_t width = x_end - x_start;
    const int32_t height = y_end - y_start;
    bool within_hold = false;
    const bool use_quality = should_use_quality_mode(internal, width, height, &within_hold);

    // color_data is DISPLAY_COLOR_FORMAT_GRAYSCALE8: row-major, 1 byte/pixel luminance
    // (0x00=black..0xFF=white, matching LVGL's L8). EPDiy wants 4bpp packed (2px/byte, 0x0=black,
    // 0xF=white); Bayer dithering (full 16-level for the quality pass, binary for MODE_DU)
    // spreads the rounding error instead of a flat truncation. epd_draw_pixel() applies this
    // panel's configured rotation per pixel (see _rotate() in epdiy.c).
    const auto* src = static_cast<const uint8_t*>(color_data);
    const size_t src_stride = static_cast<size_t>(width);

    for (int32_t row = 0; row < height; row++) {
        const uint8_t* src_row = src + static_cast<size_t>(row) * src_stride;
        const int32_t display_y = y_start + row;

        for (int32_t col = 0; col < width; col++) {
            const int32_t display_x = x_start + col;
            const uint8_t nibble = use_quality
                ? dither_to_nibble(src_row[col], display_x, display_y)
                : dither_to_bw_nibble(src_row[col], display_x, display_y);
            epd_draw_pixel(display_x, display_y, static_cast<uint8_t>(nibble << 4), internal->framebuffer);
        }
    }

    const EpdRect update_area = {
        .x = x_start,
        .y = y_start,
        .width = static_cast<uint16_t>(width),
        .height = static_cast<uint16_t>(height)
    };

    power_on(internal);
    const auto draw_mode = use_quality ? config->quality_draw_mode : FAST_DRAW_MODE;
    auto draw_result = epd_hl_update_area(
        &internal->hl_state,
        static_cast<EpdDrawMode>(draw_mode | MODE_PACKING_2PPB),
        config->temperature_celsius,
        update_area
    );

    commit_quality_mode_decision(internal, use_quality, draw_result == EPD_DRAW_SUCCESS, within_hold);
    return draw_result == EPD_DRAW_SUCCESS ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t papers3_display_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));
    if (on_off) {
        power_on(internal);
    } else if (internal->powered) {
        epd_poweroff();
        internal->powered = false;
    }
    return ERROR_NONE;
}

static DisplayColorFormat papers3_display_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_GRAYSCALE8;
}

// epd_width()/epd_height() are the panel's native, unrotated dimensions; epd_rotated_display_
// width()/height() swap them for EPD_ROT_PORTRAIT/INVERTED_PORTRAIT. draw_bitmap()'s rect and
// LVGL's canvas size are both in this rotated space; epd_draw_pixel() (in draw_bitmap()) converts
// to native space per pixel via the panel's configured rotation.
static uint16_t papers3_display_get_resolution_x(Device*) {
    return static_cast<uint16_t>(epd_rotated_display_width());
}

static uint16_t papers3_display_get_resolution_y(Device*) {
    return static_cast<uint16_t>(epd_rotated_display_height());
}

static void papers3_display_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    // Not exposed via the generic fb-direct path: EPDiy's framebuffer is its own 4bpp packed
    // format, not the DISPLAY_COLOR_FORMAT_GRAYSCALE8 (1 byte/pixel) this driver reports.
    // See get_frame_buffer_count() and draw_bitmap()'s conversion.
    *out_buffer = nullptr;
}

static uint8_t papers3_display_get_frame_buffer_count(Device*) {
    return 0;
}

// endregion

static const DisplayApi papers3_display_api = {
    // PREFER_EXTERNAL_RAM: draw_bitmap() dithers straight into internal->framebuffer (SPIRAM)
    // and never DMAs from LVGL's pointer directly, freeing LVGL's draw buffers from forced
    // internal RAM.
    .capabilities = DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLOW_REFRESH | DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
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
    .get_frame_buffer = papers3_display_get_frame_buffer,
    .get_frame_buffer_count = papers3_display_get_frame_buffer_count,
    .get_backlight = nullptr,
    .has_capability = nullptr,
};

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Papers3DisplayInternal*>(malloc(sizeof(Papers3DisplayInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->powered = false;

    epd_init(&epd_board_m5papers3, &ED047TC1, static_cast<EpdInitOptions>(EPD_LUT_1K | EPD_FEED_QUEUE_32));
    epd_set_rotation(config->rotation);

    if (!s_hl_initialized) {
        s_hl_state = epd_hl_init(EPD_BUILTIN_WAVEFORM);
        if (s_hl_state.front_fb == nullptr) {
            LOG_E(TAG, "Failed to initialize EPDiy highlevel state");
            epd_deinit();
            free(internal);
            return ERROR_RESOURCE;
        }
        s_hl_initialized = true;
    } else {
        LOG_I(TAG, "Reusing existing EPDiy highlevel state");
    }

    internal->hl_state = s_hl_state;
    internal->framebuffer = epd_hl_get_framebuffer(&internal->hl_state);

    internal->panel_pixel_count = static_cast<uint32_t>(epd_rotated_display_width()) * static_cast<uint32_t>(epd_rotated_display_height());
    internal->partial_count_since_quality = 0;
    internal->quality_hold_until_tick = 0;
    internal->quality_hold_session_start_tick = 0;

    device_set_driver_data(device, internal);

    // The bootloader/boot-logo splash draws via partial refreshes that never get a real quality
    // pass, leaving a faint ghost. Run the heavier flash-cycle clear now, before LVGL's first
    // flush ever reaches draw_bitmap(), so it never has to undo content LVGL already put on
    // screen. Nothing in the current boot path calls display_init() on any display device, so
    // this is done directly here instead of the DisplayApi init() callback. Routine post-boot
    // clears use the lighter papers3_display_clear().
    power_on(internal);
    epd_fullclear(&internal->hl_state, config->temperature_celsius);

    LOG_I(TAG, "EPDiy initialized (%dx%d native, %dx%d rotated)", epd_width(), epd_height(), epd_rotated_display_width(), epd_rotated_display_height());
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Papers3DisplayInternal*>(device_get_driver_data(device));

    if (internal->powered) {
        epd_poweroff();
        internal->powered = false;
    }

    epd_deinit();

    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

Driver papers3_display_driver = {
    .name = "papers3-display",
    .compatible = (const char*[]) { "m5stack,papers3-display", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &papers3_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &m5stack_papers3_module,
    .internal = nullptr
};

}
