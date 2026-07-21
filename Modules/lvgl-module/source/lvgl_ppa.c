// SPDX-License-Identifier: Apache-2.0
#include <tactility/lvgl_ppa.h>

#include <tactility/log.h>

#ifdef ESP_PLATFORM
#include <soc/soc_caps.h>
#endif

#if defined(ESP_PLATFORM) && SOC_PPA_SUPPORTED

#include <driver/ppa.h>
#include <esp_heap_caps.h>
#include <inttypes.h>

#define TAG "lvgl_ppa"

// PPA output buffer alignment requirement (address AND size - see lvgl_ppa_get_or_create()):
// cache line size is hardware-specific, but 64 bytes covers every current PPA-capable target's
// L1/L2 cache line size, so a fixed constant avoids needing per-SoC Kconfig lookups here.
#define LVGL_PPA_ALIGN 64
#define LVGL_PPA_ALIGN_UP(n) (((n) + (LVGL_PPA_ALIGN - 1)) & ~(size_t)(LVGL_PPA_ALIGN - 1))

struct LvglPpa {
    ppa_client_handle_t client;
    uint8_t* out_buffer;
    size_t out_buffer_size;
    bool logged_first_rotate;
};

bool lvgl_ppa_is_supported(void) {
    return true;
}

bool lvgl_ppa_supports_color_format(lv_color_format_t color_format) {
    return color_format == LV_COLOR_FORMAT_RGB565 || color_format == LV_COLOR_FORMAT_RGB888;
}

static ppa_srm_color_mode_t lvgl_ppa_color_mode(lv_color_format_t color_format) {
    return color_format == LV_COLOR_FORMAT_RGB888 ? PPA_SRM_COLOR_MODE_RGB888 : PPA_SRM_COLOR_MODE_RGB565;
}

void* lvgl_ppa_get_or_create(size_t out_buffer_size_bytes) {
    struct LvglPpa* ppa = (struct LvglPpa*)calloc(1, sizeof(struct LvglPpa));
    if (ppa == NULL) {
        return NULL;
    }

    // PPA requires the output buffer's size, not just its address, to be cache-line aligned (see
    // ppa_out_pic_blk_config_t's buffer_size doc) - round up before allocating so both match.
    size_t aligned_size = LVGL_PPA_ALIGN_UP(out_buffer_size_bytes);
    ppa->out_buffer = (uint8_t*)heap_caps_aligned_alloc(LVGL_PPA_ALIGN, aligned_size, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (ppa->out_buffer == NULL) {
        ppa->out_buffer = (uint8_t*)heap_caps_aligned_alloc(LVGL_PPA_ALIGN, aligned_size, MALLOC_CAP_DEFAULT);
    }
    if (ppa->out_buffer == NULL) {
        LOG_E(TAG, "Failed to allocate PPA output buffer (%u bytes)", (unsigned)aligned_size);
        free(ppa);
        return NULL;
    }
    ppa->out_buffer_size = aligned_size;

    ppa_client_config_t client_config = {
        .oper_type = PPA_OPERATION_SRM,
    };
    if (ppa_register_client(&client_config, &ppa->client) != ESP_OK) {
        LOG_E(TAG, "Failed to register PPA client");
        heap_caps_free(ppa->out_buffer);
        free(ppa);
        return NULL;
    }

    LOG_I(TAG, "PPA client created (out buffer: %u bytes)", (unsigned)aligned_size);
    return ppa;
}

void lvgl_ppa_delete(void* ppa_handle) {
    if (ppa_handle == NULL) {
        return;
    }
    struct LvglPpa* ppa = (struct LvglPpa*)ppa_handle;
    ppa_unregister_client(ppa->client);
    heap_caps_free(ppa->out_buffer);
    free(ppa);
}

void* lvgl_ppa_rotate(
    void* ppa_handle,
    const uint8_t* in_buff,
    int32_t w,
    int32_t h,
    lv_display_rotation_t rotation,
    lv_color_format_t color_format,
    bool swap_bytes
) {
    struct LvglPpa* ppa = (struct LvglPpa*)ppa_handle;
    ppa_srm_color_mode_t color_mode = lvgl_ppa_color_mode(color_format);
    bool swapped_wh = rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270;
    int32_t out_w = swapped_wh ? h : w;
    int32_t out_h = swapped_wh ? w : h;

    const ppa_srm_oper_config_t oper_config = {
        .in = {
            .buffer = in_buff,
            .pic_w = (uint32_t)w,
            .pic_h = (uint32_t)h,
            .block_w = (uint32_t)w,
            .block_h = (uint32_t)h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = color_mode,
        },
        .out = {
            .buffer = ppa->out_buffer,
            .buffer_size = ppa->out_buffer_size,
            .pic_w = (uint32_t)out_w,
            .pic_h = (uint32_t)out_h,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = color_mode,
        },
        // LVGL rotation is CCW, same convention as ppa_srm_rotation_angle_t - see lvgl_ppa.h.
        .rotation_angle = (ppa_srm_rotation_angle_t)rotation,
        .scale_x = 1.0f,
        .scale_y = 1.0f,
        .byte_swap = swap_bytes,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    if (ppa_do_scale_rotate_mirror(ppa->client, &oper_config) != ESP_OK) {
        LOG_E(TAG, "PPA rotate failed (%" PRId32 "x%" PRId32 " -> %" PRId32 "x%" PRId32 ")", w, h, out_w, out_h);
        return NULL;
    }

    if (!ppa->logged_first_rotate) {
        LOG_I(TAG, "PPA rotate in use (%" PRId32 "x%" PRId32 " -> %" PRId32 "x%" PRId32 ", angle=%d)", w, h, out_w, out_h, (int)rotation);
        ppa->logged_first_rotate = true;
    }

    return ppa->out_buffer;
}

#else // !(ESP_PLATFORM && SOC_PPA_SUPPORTED)

bool lvgl_ppa_is_supported(void) {
    return false;
}

bool lvgl_ppa_supports_color_format(lv_color_format_t color_format) {
    (void)color_format;
    return false;
}

void* lvgl_ppa_get_or_create(size_t out_buffer_size_bytes) {
    (void)out_buffer_size_bytes;
    return NULL;
}

void lvgl_ppa_delete(void* ppa_handle) {
    (void)ppa_handle;
}

void* lvgl_ppa_rotate(
    void* ppa_handle,
    const uint8_t* in_buff,
    int32_t w,
    int32_t h,
    lv_display_rotation_t rotation,
    lv_color_format_t color_format,
    bool swap_bytes
) {
    (void)ppa_handle;
    (void)in_buff;
    (void)w;
    (void)h;
    (void)rotation;
    (void)color_format;
    (void)swap_bytes;
    return NULL;
}

#endif
