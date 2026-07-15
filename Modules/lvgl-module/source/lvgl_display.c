// SPDX-License-Identifier: Apache-2.0
#include <tactility/lvgl_display.h>

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
    // The device's swap_xy/mirror_x/mirror_y at bind time, queried once and treated as the LV_DISPLAY_ROTATION_0 baseline.
    bool base_swap_xy;
    bool base_mirror_x;
    bool base_mirror_y;
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
    // True for DISPLAY_COLOR_FORMAT_RGB565_SWAPPED: the panel wants each 16-bit pixel high-byte-first
    // over SPI, which this little-endian CPU's native RGB565 buffer isn't. LVGL's own
    // LV_COLOR_FORMAT_RGB565_SWAPPED display format looked like the fit but isn't fully supported by
    // the SW renderer (produced garbage on real hardware) - esp_lvgl_port's own approach is instead to
    // render normal RGB565 and byte-swap the flushed rect in place just before sending it out, via
    // lv_draw_sw_rgb565_swap(). Mirrored here.
    bool byte_swap;
};

static void* lvgl_display_alloc_buffer(size_t size_bytes) {
#ifdef ESP_PLATFORM
    void* buf = heap_caps_aligned_alloc(4, size_bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (buf == NULL) {
        buf = heap_caps_aligned_alloc(4, size_bytes, MALLOC_CAP_DEFAULT);
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

// out_byte_swap is set whenever the LVGL-side buffer format doesn't already match the panel's
// wire byte order and needs a post-render swap in the flush callback (see LvglDisplayCtx::byte_swap).
static bool lvgl_display_map_color_format(enum DisplayColorFormat in, lv_color_format_t* out, bool* out_byte_swap) {
    switch (in) {
        case DISPLAY_COLOR_FORMAT_RGB565:
            *out = LV_COLOR_FORMAT_RGB565;
            *out_byte_swap = false;
            return true;
        case DISPLAY_COLOR_FORMAT_RGB565_SWAPPED:
            *out = LV_COLOR_FORMAT_RGB565;
            *out_byte_swap = true;
            return true;
        case DISPLAY_COLOR_FORMAT_RGB888:
            *out = LV_COLOR_FORMAT_RGB888;
            *out_byte_swap = false;
            return true;
        default:
            // DISPLAY_COLOR_FORMAT_BGR565/_SWAPPED: no LVGL equivalent (channel order, not byte order).
            // DISPLAY_COLOR_FORMAT_MONOCHROME: unsupported for now, no 1bpp conversion buffer implemented.
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

    int32_t x1 = area->x1;
    int32_t y1 = area->y1;
    int32_t x2 = area->x2;
    int32_t y2 = area->y2;
    uint32_t area_size_px = (uint32_t)(x2 - x1 + 1) * (uint32_t)(y2 - y1 + 1);

    lv_display_rotation_t rotation = lv_display_get_rotation(disp);
    if (ctx->sw_rotate && rotation != LV_DISPLAY_ROTATION_0) {
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

    if (ctx->owns_buffers) {
        // LVGL's area is inclusive; DisplayApi's draw_bitmap wants an exclusive end.
        display_draw_bitmap(ctx->device, x1, y1, x2 + 1, y2 + 1, color_map);
    } else if (lv_display_flush_is_last(disp)) {
        // fb-direct: a refresh cycle can call this flush_cb once per still-unjoined invalidated area
        // (lv_refr.c's refr_invalid_areas()), each writing its own tile into the *same* shared frame
        // buffer - but display_draw_bitmap() (see rgb_display_draw_bitmap()) blocks for a full
        // scan-out period whenever frame_buffer_count > 0. Presenting after every tile would pay
        // that wait N times per refresh instead of once; defer to the last flush of the cycle and
        // present the whole buffer in one call, mirroring esp_lvgl_port_disp.c's
        // lv_disp_flush_is_last() gate for its own direct/full render mode.
        uint8_t* fb_base = (uint8_t*)lvgl_display_fb_base(ctx, color_map);
        uint16_t hres = display_get_resolution_x(ctx->device);
        uint16_t vres = display_get_resolution_y(ctx->device);
        display_draw_bitmap(ctx->device, 0, 0, hres, vres, fb_base);
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
    bool byte_swap;
    enum DisplayColorFormat kernel_color_format = display_get_color_format(device);
    if (!lvgl_display_map_color_format(kernel_color_format, &lv_color_format, &byte_swap)) {
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
    ctx->byte_swap = byte_swap;
    ctx->sw_rotate = config->sw_rotate;
    ctx->has_swap_xy_cap = display_has_capability(device, DISPLAY_CAPABILITY_CAP_SWAP_XY);
    ctx->has_mirror_cap = display_has_capability(device, DISPLAY_CAPABILITY_CAP_MIRROR);
    // fb_count > 0 with capability false means a driver reports mirror/swap_xy unavailable
    // specifically because binding fb-direct would defeat them (see rgb_display_has_capability()).
    // We're about to fall back to an owned buffer instead (right below) precisely because of that,
    // at which point the concern doesn't apply anymore: frame_buffer_count > 0 mattering to
    // has_capability() at all implies the driver's mirror()/swap_xy() are real, non-null
    // implementations, and the copy-into-fb path honors them correctly once not fb-direct-bound.
    // sw_rotate is excluded too: it writes rotated pixels into ctx->rotate_buf, which
    // lvgl_display_fb_base() doesn't recognize, so fb-direct must stay off in that case as well.
    bool would_bind_fb_direct = fb_count > 0 && ctx->has_swap_xy_cap && ctx->has_mirror_cap && !ctx->sw_rotate;
    if (fb_count > 0 && !would_bind_fb_direct) {
        ctx->has_swap_xy_cap = true;
        ctx->has_mirror_cap = true;
    }
    ctx->base_swap_xy = ctx->has_swap_xy_cap ? display_get_swap_xy(device) : false;
    ctx->base_mirror_x = ctx->has_mirror_cap ? display_get_mirror_x(device) : false;
    ctx->base_mirror_y = ctx->has_mirror_cap ? display_get_mirror_y(device) : false;

    lv_display_render_mode_t render_mode;
    size_t buf_size_bytes;

    if (would_bind_fb_direct) {
        display_get_frame_buffer(device, 0, &ctx->buf1);
        if (fb_count > 1) {
            display_get_frame_buffer(device, 1, &ctx->buf2);
        }
        ctx->owns_buffers = false;
        render_mode = LV_DISPLAY_RENDER_MODE_FULL;
        buf_size_bytes = (size_t)hres * vres * bpp;
    } else {
        uint16_t buf_height = config->buffer_height > 0 ? config->buffer_height : vres;
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
        render_mode = LV_DISPLAY_RENDER_MODE_PARTIAL;
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
