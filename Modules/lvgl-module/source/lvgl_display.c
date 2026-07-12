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

    display_swap_xy(ctx->device, swap_xy);
    display_mirror(ctx->device, mirror_x, mirror_y);
}

static void lvgl_display_rotation_event_cb(lv_event_t* e) {
    struct LvglDisplayCtx* ctx = (struct LvglDisplayCtx*)lv_event_get_user_data(e);
    lv_display_t* disp = (lv_display_t*)lv_event_get_current_target(e);
    lvgl_display_apply_rotation(ctx, lv_display_get_rotation(disp));
}

static void lvgl_display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* color_map) {
    struct LvglDisplayCtx* ctx = (struct LvglDisplayCtx*)lv_display_get_driver_data(disp);
    if (ctx->byte_swap) {
        lv_draw_sw_rgb565_swap(color_map, lv_area_get_size(area));
    }
    // LVGL's area is inclusive; DisplayApi's draw_bitmap wants an exclusive end.
    display_draw_bitmap(ctx->device, area->x1, area->y1, area->x2 + 1, area->y2 + 1, color_map);
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
    ctx->base_swap_xy = display_get_swap_xy(device);
    ctx->base_mirror_x = display_get_mirror_x(device);
    ctx->base_mirror_y = display_get_mirror_y(device);

    lv_display_render_mode_t render_mode;
    size_t buf_size_bytes;

    if (fb_count > 0) {
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

    lv_display_t* disp = lv_display_create(hres, vres);
    if (disp == NULL) {
        if (ctx->owns_buffers) {
            lvgl_display_free_buffer(ctx->buf1);
            lvgl_display_free_buffer(ctx->buf2);
        }
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
        free(ctx);
    }
}
