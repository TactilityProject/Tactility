#include <Tactility/app/terminal/TerminalRendererGeneric.h>

#include <tactility/drivers/display.h>
#include <tactility/log.h>

#ifdef ESP_PLATFORM
#include <esp_cache.h>
#endif

#include <cstring>

constexpr auto* TAG = "TermRenderGen";

TerminalRendererGeneric::~TerminalRendererGeneric() {
    end();
}

bool TerminalRendererGeneric::begin(Device* displayDevice) {
    panelWidth = display_get_resolution_x(displayDevice);
    panelHeight = display_get_resolution_y(displayDevice);

    // No rotation hardware here, so the terminal is drawn in the panel's own orientation.
    frameWidth = panelWidth;
    frameHeight = panelHeight;

    if (!allocateCommon(displayDevice)) {
        return false;
    }

    // Best-effort: falls back to pushing frameBuffer directly in present() if unavailable.
    acquireHwDoubleBuffer();

    LOG_I(TAG, "Terminal %dx%d cells (%dx%d px) on %dx%d panel",
             cols, rowCount, cellWidth, cellHeight, panelWidth, panelHeight);
    return true;
}

void TerminalRendererGeneric::end() {
    freeCommon();
}

void TerminalRendererGeneric::present() {
    const size_t frameBytes = static_cast<size_t>(frameWidth) * frameHeight * sizeof(uint16_t);
#ifdef ESP_PLATFORM
    esp_cache_msync(frameBuffer, frameBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif

    if (usingHwFrameBuffer) {
        uint16_t* out = hwFrameBuffers[backBufferIndex];
        memcpy(out, frameBuffer, frameBytes);
#ifdef ESP_PLATFORM
        esp_cache_msync(out, frameBytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
#endif
        display_draw_bitmap(display, 0, 0, frameWidth, frameHeight, out);
        backBufferIndex = 1 - backBufferIndex;
    } else {
        display_draw_bitmap(display, 0, 0, frameWidth, frameHeight, frameBuffer);
    }
}
