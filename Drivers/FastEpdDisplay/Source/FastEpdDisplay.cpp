#include "FastEpdDisplay.h"

#include <tactility/log.h>

#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

#define TAG "FastEpdDisplay"

FastEpdDisplay::~FastEpdDisplay() {
    stop();
}

void FastEpdDisplay::flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    auto* self = static_cast<FastEpdDisplay*>(lv_display_get_user_data(disp));

    static uint32_t s_flush_log_counter = 0;

    const int32_t width = area->x2 - area->x1 + 1;
    const bool grayscale4bpp = self->configuration.use4bppGrayscale;

    // LVGL logical resolution is portrait (540x960). FastEPD PaperS3 native is landscape (960x540).
    // Keep FastEPD at rotation 0 and do the coordinate transform ourselves.
    // For a 90° clockwise transform:
    //  x_native = y
    //  y_native = (native_height - 1) - x
    const int native_width = self->epd.width();
    const int native_height = self->epd.height();

    // Compute the native line range that will be affected by this flush.
    // With our mapping y_native = (native_height - 1) - x
    // So x range maps to y_native range.
    int start_line = (native_height - 1) - (int)area->x2;
    int end_line = (native_height - 1) - (int)area->x1;
    if (start_line > end_line) {
        const int tmp = start_line;
        start_line = end_line;
        end_line = tmp;
    }
    if (start_line < 0) start_line = 0;
    if (end_line >= native_height) end_line = native_height - 1;

    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            const uint8_t gray8 = px_map[(y - area->y1) * width + (x - area->x1)];
            const uint8_t color = grayscale4bpp ? (uint8_t)(gray8 >> 4) : (uint8_t)((gray8 > 127) ? BBEP_BLACK : BBEP_WHITE);

            const int x_native = y;
            const int y_native = (native_height - 1) - x;

            // Be defensive: any out-of-range drawPixelFast will corrupt memory.
            if (x_native < 0 || x_native >= native_width || y_native < 0 || y_native >= native_height) {
                continue;
            }
            self->epd.drawPixelFast(x_native, y_native, color);
        }
    }

    if (start_line <= end_line) {
        (void)self->epd.einkPower(1);
        self->flushCount++;
        const uint32_t cadence = self->configuration.fullRefreshEveryNFlushes;
        const bool requested_full = self->forceNextFullRefresh.exchange(false);
        const bool do_full = requested_full || ((cadence > 0) && (self->flushCount % cadence == 0));

        const bool should_log = ((++s_flush_log_counter % 25U) == 0U);
        if (should_log) {
            LOG_I(TAG, "flush #%lu area=(%ld,%ld)-(%ld,%ld) lines=[%d..%d] full=%d",
                (unsigned long)self->flushCount,
                (long)area->x1, (long)area->y1, (long)area->x2, (long)area->y2,
                start_line, end_line, (int)do_full);
        }

        if (do_full) {
            const int rc = self->epd.fullUpdate(CLEAR_FAST, true, nullptr);
            if (should_log) {
                LOG_I(TAG, "fullUpdate rc=%d", rc);
            }

            // After a full update, keep FastEPD's previous/current buffers in sync so that
            // subsequent partial updates compute correct diffs.
            const int w = self->epd.width();
            const int h = self->epd.height();
            const size_t bytes_per_row = grayscale4bpp
                ? (size_t)(w + 1) / 2
                : (size_t)(w + 7) / 8;
            const size_t plane_size = bytes_per_row * (size_t)h;
            if (self->epd.currentBuffer() && self->epd.previousBuffer()) {
                memcpy(self->epd.previousBuffer(), self->epd.currentBuffer(), plane_size);
            }
        } else {
            if (grayscale4bpp) {
                // FastEPD partialUpdate only supports 1bpp mode.
                // For 4bpp we currently do a fullUpdate. Region-based updates are tricky here because
                // we also manually rotate/transform pixels in flush_cb; a mismatched rect can refresh
                // the wrong strip of the panel (seen as "split" buttons on the final refresh).
                const int rc = self->epd.fullUpdate(CLEAR_FAST, true, nullptr);
                if (should_log) {
                    LOG_I(TAG, "fullUpdate(4bpp) rc=%d", rc);
                }
            } else {
                const int rc = self->epd.partialUpdate(true, start_line, end_line);

                // Keep FastEPD's previous/current buffers in sync after partial updates as well.
                // This avoids stale diffs where subsequent updates don't visibly apply.
                const int w = self->epd.width();
                const int h = self->epd.height();
                const size_t bytes_per_row = (size_t)(w + 7) / 8;
                const size_t plane_size = bytes_per_row * (size_t)h;
                if (rc == BBEP_SUCCESS && self->epd.currentBuffer() && self->epd.previousBuffer()) {
                    memcpy(self->epd.previousBuffer(), self->epd.currentBuffer(), plane_size);
                }

                if (should_log) {
                    LOG_I(TAG, "partialUpdate rc=%d", rc);
                }
            }
        }
    }

    lv_display_flush_ready(disp);
}

bool FastEpdDisplay::start() {
    if (initialized) {
        return true;
    }

    const int rc = epd.initPanel(BB_PANEL_M5PAPERS3, configuration.busSpeedHz);
    if (rc != BBEP_SUCCESS) {
        LOG_E(TAG, "FastEPD initPanel failed rc=%d", rc);
        return false;
    }

    LOG_I(TAG, "FastEPD native size %dx%d", epd.width(), epd.height());

    const int desired_mode = configuration.use4bppGrayscale ? BB_MODE_4BPP : BB_MODE_1BPP;
    if (epd.setMode(desired_mode) != BBEP_SUCCESS) {
        LOG_E(TAG, "FastEPD setMode(%d) failed", desired_mode);
        return false;
    }

    // Keep FastEPD at rotation 0. LVGL-to-native mapping is handled in flush_cb.

    // Ensure previous/current buffers are in sync and the panel starts from a known state.
    if (epd.einkPower(1) != BBEP_SUCCESS) {
        LOG_W(TAG, "FastEPD einkPower(1) failed");
    } else {
        epd.fillScreen(configuration.use4bppGrayscale ? 0x0F : BBEP_WHITE);

        const int native_width = epd.width();
        const int native_height = epd.height();
        const size_t bytes_per_row = configuration.use4bppGrayscale
            ? (size_t)(native_width + 1) / 2
            : (size_t)(native_width + 7) / 8;
        const size_t plane_size = bytes_per_row * (size_t)native_height;

        if (epd.currentBuffer() && epd.previousBuffer()) {
            memcpy(epd.previousBuffer(), epd.currentBuffer(), plane_size);
        }

        if (epd.fullUpdate(CLEAR_FAST, true, nullptr) != BBEP_SUCCESS) {
            LOG_W(TAG, "FastEPD fullUpdate failed");
        }
    }

    initialized = true;
    return true;
}

bool FastEpdDisplay::stop() {
    if (lvglDisplay) {
        stopLvgl();
    }

    if (initialized) {
        epd.deInit();
        initialized = false;
    }

    return true;
}

bool FastEpdDisplay::startLvgl() {
    if (lvglDisplay != nullptr) {
        return true;
    }

    lvglDisplay = lv_display_create(configuration.horizontalResolution, configuration.verticalResolution);
    if (lvglDisplay == nullptr) {
        return false;
    }

    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_L8);

    if (lv_display_get_rotation(lvglDisplay) != LV_DISPLAY_ROTATION_0) {
        lv_display_set_rotation(lvglDisplay, LV_DISPLAY_ROTATION_0);
    }

    const uint32_t pixel_count = (uint32_t)(configuration.horizontalResolution * configuration.verticalResolution / 10);
    const uint32_t buf_size = pixel_count * (uint32_t)lv_color_format_get_size(LV_COLOR_FORMAT_L8);
    lvglBufSize = buf_size;

#ifdef ESP_PLATFORM
    lvglBuf1 = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    lvglBuf1 = malloc(buf_size);
#endif

    if (lvglBuf1 == nullptr) {
        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
        return false;
    }

    lvglBuf2 = nullptr;
    lv_display_set_buffers(lvglDisplay, lvglBuf1, lvglBuf2, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_set_user_data(lvglDisplay, this);
    lv_display_set_flush_cb(lvglDisplay, FastEpdDisplay::flush_cb);

    if (configuration.touch && configuration.touch->supportsLvgl()) {
        configuration.touch->startLvgl(lvglDisplay);
    }

    return true;
}

bool FastEpdDisplay::stopLvgl() {
    if (lvglDisplay) {
        if (configuration.touch) {
            configuration.touch->stopLvgl();
        }

        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
    }

    if (lvglBuf1 != nullptr) {
        free(lvglBuf1);
        lvglBuf1 = nullptr;
    }

    if (lvglBuf2 != nullptr) {
        free(lvglBuf2);
        lvglBuf2 = nullptr;
    }

    lvglBufSize = 0;

    return true;
}
