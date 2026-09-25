#include <Tactility/app/terminal/TerminalRendererGeneric.h>

#include <graphics/pixel_buffer.h>

#include <tactility/drivers/display.h>
#include <tactility/log.h>

constexpr auto* TAG = "TermRenderGen";

TerminalRendererGeneric::~TerminalRendererGeneric() {
    end();
}

bool TerminalRendererGeneric::begin(Device* displayDevice) {
    panelWidth = display_get_resolution_x(displayDevice);
    panelHeight = display_get_resolution_y(displayDevice);

    frameWidth = panelWidth;
    frameHeight = panelHeight;

    if (!allocateCommon(displayDevice)) {
        return false;
    }

    acquireHwDoubleBuffer(); // best-effort; present() falls back to pushing frameBuffer directly

    if (!allocateFullFrameBufferIfNeeded()) {
        freeCommon();
        return false;
    }

    clearPanelOnce();

    LOG_I(TAG, "Terminal %dx%d cells (%dx%d px) on %dx%d panel",
             cols, rowCount, cellWidth, cellHeight, panelWidth, panelHeight);
    return true;
}

void TerminalRendererGeneric::end() {
    freeCommon();
}

void TerminalRendererGeneric::present(int yStart, int yEnd) {
    pixel_buffer_msync(frameBuffer, 0, 0, frameWidth, yEnd - yStart);
    presentRegion(frameBuffer, 0, yStart, frameWidth, yEnd - yStart);
}
