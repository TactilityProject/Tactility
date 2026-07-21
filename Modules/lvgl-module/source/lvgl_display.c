// SPDX-License-Identifier: Apache-2.0
#include <tactility/lvgl_display.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/log.h>

#include <stdlib.h>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#define TAG "lvgl_display"

struct LvglDisplayCtx {
    struct Device* device;
    void* buf1;
    void* buf2;
    bool owns_buffers; // false when buf1/buf2 point at the device's own frame buffer(s)
    // Mirrors what lvgl_display_add() passed to lv_display_set_render_mode() - there's no
    // lv_display_get_render_mode() to query it back from LVGL, so it's cached here instead.
    lv_display_render_mode_t render_mode;
    // The device's swap_xy/mirror_x/mirror_y at bind time, queried once and treated as the LV_DISPLAY_ROTATION_0 baseline.
    bool base_swap_xy;
    bool base_mirror_x;
    bool base_mirror_y;
    // The device's configured gap at bind time, in the same LV_DISPLAY_ROTATION_0 baseline frame
    // as base_swap_xy above. set_gap() is a raw (x,y) offset applied to whatever coordinates the
    // panel is currently being drawn with - it has no idea about swap_xy, so a rotation that flips
    // swap_xy relative to this baseline must swap gap_x/gap_y too (see lvgl_display_apply_rotation()).
    int32_t base_gap_x;
    int32_t base_gap_y;
    bool has_set_gap_cap;
    // When true, rotation is done in software in the flush callback instead of via display_swap_xy()/
    // display_mirror(); rotate_buf holds the rotated pixels and is sized like buf1.
    bool sw_rotate;
    void* rotate_buf;
    // Size of buf1/buf2 (each) - used by lvgl_display_fb_base() to range-check which real buffer
    // (for the fb-direct case) a given color_map pointer falls into.
    size_t buf_size_bytes;
    // Cached DISPLAY_CAPABILITY_CAP_SWAP_XY/CAP_MIRROR: swap_xy()/mirror() and their getters are
    // null on drivers that don't support them, so rotation handling must not call through blindly.
    bool has_swap_xy_cap;
    bool has_mirror_cap;
    // Mirrors LvglDisplayConfig::swap_bytes: the panel is big endian while the OS is little endian,
    // so we fix it in software. In the future, the driver should probably expose endianness requirements instead.
    bool byte_swap;
};

static void* lvgl_display_alloc_buffer(size_t size_bytes) {
#ifdef ESP_PLATFORM
    // Must match LV_DRAW_BUF_ALIGN (can be > 4 - e.g. 64, tied to the cache line size for
    // DMA2D/PPA coherency on some targets - see sdkconfig's CONFIG_LV_DRAW_BUF_ALIGN). A buffer
    // allocated less strictly than that fails lv_display_set_buffers()'s alignment assert, which
    // is configured to LV_ASSERT_HANDLER (while(1);) rather than a clean abort - i.e. a silent hang.
    void* buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size_bytes, MALLOC_CAP_DEFAULT);
    }
    return buf;
#else
    return malloc(size_bytes);
#endif
}

static void lvgl_display_free_buffer(void* buf) {
#ifdef ESP_PLATFORM
    heap_caps_free(buf);
#else
    free(buf);
#endif
}

// Resolves the kernel-reported color format to an LVGL color format. RGB565 and BGR565 (and
// their _SWAPPED variants) all render identically as far as LVGL is concerned - it has no native
// concept of channel order, only byte order (see LvglDisplayConfig::swap_bytes for that axis).
static bool lvgl_display_map_color_format(enum DisplayColorFormat in, lv_color_format_t* out) {
    switch (in) {
        case DISPLAY_COLOR_FORMAT_RGB565:
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
        case DISPLAY_COLOR_FORMAT_BGR565:
        case DISPLAY_COLOR_FORMAT_BGR565_SWAPPED:
            *out = LV_COLOR_FORMAT_RGB565;
            return true;
        case DISPLAY_COLOR_FORMAT_RGB888:
            *out = LV_COLOR_FORMAT_RGB888;
            return true;
        case DISPLAY_COLOR_FORMAT_MONOCHROME:
            // Row-major, MSB-first 1bpp (matches LV_COLOR_FORMAT_I1's raw layout once the
            // palette header is stripped, see lvgl_display_flush_cb()) - any page/tile
            // reformatting a specific panel's GDDRAM needs is that driver's own concern
            // (e.g. ssd1306_draw_bitmap()'s row-to-page transpose).
            *out = LV_COLOR_FORMAT_I1;
            return true;
        default:
            return false;
    }
}

static void lvgl_display_apply_rotation(struct LvglDisplayCtx* ctx, lv_display_rotation_t rotation) {
    // SW-rotated displays stay in their base orientation; rotation is applied per-flush instead.
    if (ctx->sw_rotate) {
        return;
    }

    bool swap_xy = ctx->base_swap_xy;
    bool mirror_x = ctx->base_mirror_x;
    bool mirror_y = ctx->base_mirror_y;

    switch (rotation) {
        case LV_DISPLAY_ROTATION_0:
            break;
        case LV_DISPLAY_ROTATION_90:
            swap_xy = !ctx->base_swap_xy;
            if (ctx->base_swap_xy) {
                mirror_x = !ctx->base_mirror_x;
            } else {
                mirror_y = !ctx->base_mirror_y;
            }
            break;
        case LV_DISPLAY_ROTATION_180:
            mirror_x = !ctx->base_mirror_x;
            mirror_y = !ctx->base_mirror_y;
            break;
        case LV_DISPLAY_ROTATION_270:
            swap_xy = !ctx->base_swap_xy;
            if (ctx->base_swap_xy) {
                mirror_y = !ctx->base_mirror_y;
            } else {
                mirror_x = !ctx->base_mirror_x;
            }
            break;
    }

    if (ctx->has_swap_xy_cap) {
        display_swap_xy(ctx->device, swap_xy);
    }
    if (ctx->has_mirror_cap) {
        display_mirror(ctx->device, mirror_x, mirror_y);
    }
    if (ctx->has_set_gap_cap) {
        // set_gap() takes its (x,y) in whatever axes the panel is currently drawn with, not the
        // baseline's - swap gap_x/gap_y whenever this rotation's swap_xy differs from the baseline.
        bool gap_axes_swapped = swap_xy != ctx->base_swap_xy;
        int32_t gap_x = gap_axes_swapped ? ctx->base_gap_y : ctx->base_gap_x;
        int32_t gap_y = gap_axes_swapped ? ctx->base_gap_x : ctx->base_gap_y;
        display_set_gap(ctx->device, gap_x, gap_y);
    }
}

static void lvgl_display_rotation_event_cb(lv_event_t* e) {
    struct LvglDisplayCtx* ctx = (struct LvglDisplayCtx*)lv_event_get_user_data(e);
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);
    lvgl_display_apply_rotation(ctx, lv_display_get_rotation(disp));
}

// Returns which of buf1/buf2 (the real frame buffers, when !owns_buffers) color_map falls inside.
// Defaults to buf1, which also covers the single-frame-buffer (buf2 == NULL) case.
static void* lvgl_display_fb_base(struct LvglDisplayCtx* ctx, const uint8_t* color_map) {
    if (ctx->buf2 != NULL && color_map >= (uint8_t*)ctx->buf2 &&
        color_map < (uint8_t*)ctx->buf2 + ctx->buf_size_bytes) {
        return ctx->buf2;
    }
    return ctx->buf1;
}

    static void lvgl_display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_map) {
    struct LvglDisplayCtx* ctx = (struct LvglDisplayCtx*)lv_display_get_driver_data(disp);
    bool is_i1 = lv_display_get_color_format(disp) == LV_COLOR_FORMAT_I1;

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    uint32_t area_size_px = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);

    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    bool rotating = ctx->sw_rotate && rotation != LV_DISPLAY_ROTATION_0;

    // In FULL mode, a refresh cycle can call this once per still-unjoined invalidated area (see
    // the comment below) before the frame is complete - rotating per-tile here would only ever
    // reflect the last tile written, not the accumulated whole frame. Rotate the whole buffer in
    // one shot instead, right before presenting (see the FULL-mode branch below).
    if (rotating && ctx->render_mode != LV_DISPLAY_RENDER_MODE_FULL) {
        // sw_rotate is only ever requested for displays lacking real HW mirror/swap_xy capability
        // (see lvgl_devices.c), and lvgl_display_add() only binds fb-direct (owns_buffers == false)
        // when that capability is present - so this is always the owns_buffers == true case.
        lv_color_format_t color_format = lv_display_get_color_format(disp);
        int32_t w = x2 - x1 + 1;
        int32_t h = y2 - y1 + 1;
        uint32_t w_stride = lv_draw_buf_width_to_stride(w, color_format);
        uint32_t h_stride = lv_draw_buf_width_to_stride(h, color_format);
        if (rotation == LV_DISPLAY_ROTATION_180) {
            lv_draw_sw_rotate(color_map, ctx->rotate_buf, w, h, w_stride, w_stride, rotation, color_format);
        } else {
            lv_draw_sw_rotate(color_map, ctx->rotate_buf, w, h, w_stride, h_stride, rotation, color_format);
        }
        color_map = (uint8_t*)ctx->rotate_buf;
        lv_area_t rotated_area = { x1, y1, x2, y2 };
        lv_display_rotate_area(disp, &rotated_area);
        x1 = rotated_area.x1;
        y1 = rotated_area.y1;
        x2 = rotated_area.x2;
        y2 = rotated_area.y2;
    }

    if (ctx->byte_swap) {
        lv_draw_sw_rgb565_swap(color_map, area_size_px);
    }

    if (ctx->render_mode == LV_DISPLAY_RENDER_MODE_FULL) {
        // FULL mode always redraws (and flushes) the whole display, but a refresh cycle can still
        // call this flush_cb once per still-unjoined invalidated area (lv_refr.c's
        // refr_invalid_areas()), each writing its own tile into the *same* shared buffer - only
        // the last call actually holds the complete frame. This applies equally whether that
        // buffer is one we own (owns_buffers, e.g. an I1 e-paper/OLED panel) or a real hardware
        // frame buffer (fb-direct, where display_draw_bitmap() - see rgb_display_draw_bitmap() -
        // additionally blocks for a full scan-out period whenever frame_buffer_count > 0).
        // Presenting on every call would send partially-rendered frames, and for fb-direct would
        // also pay that scan-out wait N times per refresh instead of once; defer to the last
        // flush and present the whole buffer in one call, mirroring esp_lvgl_port_disp.c's own
        // lv_disp_flush_is_last() gate for its direct/full render mode.
        if (lv_display_flush_is_last(disp)) {
            uint8_t* fb_base;
            if (ctx->owns_buffers) {
                fb_base = (uint8_t*)ctx->buf1;
                if (is_i1) {
                    // LVGL reserves an 8-byte palette (2 x lv_color32_t) at the front of every I1
                    // draw buffer; it's on the caller to skip it before treating the rest as
                    // pixel data.
                    fb_base += 8;
                }
            } else {
                fb_base = (uint8_t*)lvgl_display_fb_base(ctx, color_map);
            }
            uint16_t hres = display_get_resolution_x(ctx->device);
            uint16_t vres = display_get_resolution_y(ctx->device);

            if (rotating) {
                // fb_base now holds the whole completed frame, but still in LVGL's *logical*
                // (rotated) w/h - rotate it as one block into rotate_buf, matching the panel's
                // fixed physical w/h, before presenting.
                lv_color_format_t color_format = lv_display_get_color_format(disp);
                bool swapped_wh = rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270;
                int32_t logical_w = swapped_wh ? (int32_t)vres : (int32_t)hres;
                int32_t logical_h = swapped_wh ? (int32_t)hres : (int32_t)vres;
                uint32_t src_stride = lv_draw_buf_width_to_stride(logical_w, color_format);
                uint32_t dest_stride = lv_draw_buf_width_to_stride(hres, color_format);
                lv_draw_sw_rotate(fb_base, ctx->rotate_buf, logical_w, logical_h, src_stride, dest_stride, rotation, color_format);
                fb_base = (uint8_t*)ctx->rotate_buf;
            }

            display_draw_bitmap(ctx->device, 0, 0, hres, vres, fb_base);
        }
    } else if (ctx->owns_buffers) {
        // PARTIAL mode: each flush_cb call is one independent, complete tile into a buffer that
        // gets reused for the next tile, so present it immediately rather than waiting.
        // LVGL's area is inclusive; DisplayApi's draw_bitmap wants an exclusive end.
        display_draw_bitmap(ctx->device, x1, y1, x2 + 1, y2 + 1, color_map);
    }
    // DisplayApi has no async completion callback, so draw_bitmap is synchronous.
    lv_display_flush_ready(disp);
}

error_t lvgl_display_add(struct Device* device, const struct LvglDisplayConfig* config, lv_display_t** out_display) {
    if (device == NULL || config == NULL || out_display == NULL) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (device_get_type(device) != &DISPLAY_TYPE) {
        return ERROR_INVALID_ARGUMENT;
    }

    lv_color_format_t lv_color_format;
    enum DisplayColorFormat kernel_color_format = display_get_color_format(device);
    if (!lvgl_display_map_color_format(kernel_color_format, &lv_color_format)) {
        LOG_E(TAG, "Unsupported color format %d (no LVGL equivalent)", (int)kernel_color_format);
        return ERROR_NOT_SUPPORTED;
    }

    uint16_t hres = display_get_resolution_x(device);
    uint16_t vres = display_get_resolution_y(device);
    uint8_t fb_count = display_get_frame_buffer_count(device);
    uint8_t bpp = lv_color_format_get_size(lv_color_format);

    struct LvglDisplayCtx* ctx = (struct LvglDisplayCtx*)calloc(1, sizeof(struct LvglDisplayCtx));
    if (ctx == NULL) {
        return ERROR_OUT_OF_MEMORY;
    }
    ctx->device = device;
    ctx->byte_swap = config->swap_bytes;
    ctx->sw_rotate = config->sw_rotate;
    ctx->has_swap_xy_cap = display_has_capability(device, DISPLAY_CAPABILITY_CAP_SWAP_XY);
    ctx->has_mirror_cap = display_has_capability(device, DISPLAY_CAPABILITY_CAP_MIRROR);
    ctx->has_set_gap_cap = display_has_capability(device, DISPLAY_CAPABILITY_CAP_SET_GAP);
    // sw_rotate is excluded from fb-direct binding below: it writes rotated pixels into
    // ctx->rotate_buf, which lvgl_display_fb_base() doesn't recognize, so fb-direct must stay
    // off in that case as well.
    bool would_bind_fb_direct = fb_count > 0 && ctx->has_swap_xy_cap && ctx->has_mirror_cap && !ctx->sw_rotate;
    if (fb_count > 0 && !would_bind_fb_direct) {
        // Only re-enable capabilities the driver reported off above if it has a *dynamic*
        // has_capability() (DisplayApi.has_capability non-null, e.g. rgb_display_has_capability()/
        // st7701_has_capability()): that's specifically what a driver implements when a capability's
        // availability is state-dependent - here, off only because binding fb-direct would defeat
        // it (see rgb_display_has_capability()) - so once we're falling back to an owned buffer
        // instead (right below), the concern no longer applies. A driver with no dynamic
        // has_capability() reports fixed, hardware-level capabilities instead: if the bit's off,
        // swap_xy()/mirror() don't exist at all (null function pointers - see the DisplayApi
        // contract), so forcing them back on here would call through a null pointer.
        const struct DisplayApi* api = (const struct DisplayApi*)device_get_driver(device)->api;
        if (api->has_capability != NULL) {
            ctx->has_swap_xy_cap = true;
            ctx->has_mirror_cap = true;
        }
    }
    ctx->base_swap_xy = ctx->has_swap_xy_cap ? display_get_swap_xy(device) : false;
    ctx->base_mirror_x = ctx->has_mirror_cap ? display_get_mirror_x(device) : false;
    ctx->base_mirror_y = ctx->has_mirror_cap ? display_get_mirror_y(device) : false;
    ctx->base_gap_x = ctx->has_set_gap_cap ? display_get_gap_x(device) : 0;
    ctx->base_gap_y = ctx->has_set_gap_cap ? display_get_gap_y(device) : 0;

    lv_display_render_mode_t render_mode;
    size_t buf_size_bytes;

    if (lv_color_format == LV_COLOR_FORMAT_I1) {
        // I1 packs 8 pixels/byte row-wise and LVGL reserves an 8-byte palette header at the
        // buffer's start (see lvgl_display_flush_cb()). Always redraw the whole frame in one
        // owned buffer instead of computing partial-region byte offsets against that packing.
        buf_size_bytes = (size_t)((hres + 7) / 8) * vres + 8;
        ctx->buf1 = lvgl_display_alloc_buffer(buf_size_bytes);
        if (ctx->buf1 == NULL) {
            free(ctx);
            return ERROR_OUT_OF_MEMORY;
        }
        ctx->owns_buffers = true;
        render_mode = LV_DISPLAY_RENDER_MODE_FULL;
    } else if (would_bind_fb_direct) {
        display_get_frame_buffer(device, 0, &ctx->buf1);
        if (fb_count > 1) {
            display_get_frame_buffer(device, 1, &ctx->buf2);
        }
        ctx->owns_buffers = false;
        render_mode = LV_DISPLAY_RENDER_MODE_FULL;
        buf_size_bytes = (size_t)hres * vres * bpp;
    } else {
        uint16_t buf_height = config->force_full_frame || config->buffer_height == 0
            ? vres : config->buffer_height;
        buf_size_bytes = (size_t)hres * buf_height * bpp;

        ctx->buf1 = lvgl_display_alloc_buffer(buf_size_bytes);
        if (ctx->buf1 == NULL) {
            free(ctx);
            return ERROR_OUT_OF_MEMORY;
        }
        if (config->double_buffer) {
            ctx->buf2 = lvgl_display_alloc_buffer(buf_size_bytes);
            if (ctx->buf2 == NULL) {
                lvgl_display_free_buffer(ctx->buf1);
                free(ctx);
                return ERROR_OUT_OF_MEMORY;
            }
        }
        ctx->owns_buffers = true;
        render_mode = config->force_full_frame ? LV_DISPLAY_RENDER_MODE_FULL : LV_DISPLAY_RENDER_MODE_PARTIAL;
    }

    ctx->buf_size_bytes = buf_size_bytes;

    if (ctx->sw_rotate) {
        ctx->rotate_buf = lvgl_display_alloc_buffer(buf_size_bytes);
        if (ctx->rotate_buf == NULL) {
            if (ctx->owns_buffers) {
                lvgl_display_free_buffer(ctx->buf1);
                lvgl_display_free_buffer(ctx->buf2);
            }
            free(ctx);
            return ERROR_OUT_OF_MEMORY;
        }
    }

    lv_display_t* disp = lv_display_create(hres, vres);
    if (disp == NULL) {
        if (ctx->owns_buffers) {
            lvgl_display_free_buffer(ctx->buf1);
            lvgl_display_free_buffer(ctx->buf2);
        }
        lvgl_display_free_buffer(ctx->rotate_buf);
        free(ctx);
        return ERROR_OUT_OF_MEMORY;
    }

    ctx->render_mode = render_mode;
    lv_display_set_color_format(disp, lv_color_format);
    lv_display_set_buffers(disp, ctx->buf1, ctx->buf2, buf_size_bytes, render_mode);
    lv_display_set_flush_cb(disp, lvgl_display_flush_cb);
    lv_display_set_driver_data(disp, ctx);
    lv_display_add_event_cb(disp, lvgl_display_rotation_event_cb, LV_EVENT_RESOLUTION_CHANGED, ctx);

    // Apply once explicitly, independent of whether LV_EVENT_RESOLUTION_CHANGED fires on creation.
    lvgl_display_apply_rotation(ctx, lv_display_get_rotation(disp));

    *out_display = disp;
    return ERROR_NONE;
}

void lvgl_display_remove(lv_display_t* display) {
    if (display == NULL) {
        return;
    }

    struct LvglDisplayCtx* ctx = (struct LvglDisplayCtx*)lv_display_get_driver_data(display);
    lv_display_delete(display);

    if (ctx != NULL) {
        if (ctx->owns_buffers) {
            if (ctx->buf1 != NULL) {
                lvgl_display_free_buffer(ctx->buf1);
            }
            if (ctx->buf2 != NULL) {
                lvgl_display_free_buffer(ctx->buf2);
            }
        }
        if (ctx->rotate_buf != NULL) {
            lvgl_display_free_buffer(ctx->rotate_buf);
        }
        free(ctx);
    }
}
