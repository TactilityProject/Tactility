#include "Gdeq031t10Display.h"

#include <tactility/log.h>

#include <algorithm>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <themes/mono/lv_theme_mono.h>
// Full lv_theme_t definition (opaque in public lvgl.h) — needed to extend the
// mono theme with a custom apply callback, per LVGL's theme-extension pattern.
#include <themes/lv_theme_private.h>

constexpr auto* TAG = "GDEQ031T10";

// UC8253-family commands, ported from Xinyuan-LilyGO/T-Deck-MAX's
// Display_EPD_W21.cpp reference driver.
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
}

void Gdeq031t10Display::writeCommand(uint8_t command) {
    gpio_set_level(configuration->pinDc, 0);
    spi_transaction_t transaction = {};
    transaction.length = 8;
    transaction.tx_buffer = &command;
    spi_device_polling_transmit(spiDevice, &transaction);
}

void Gdeq031t10Display::writeData(const uint8_t* data, size_t length) {
    gpio_set_level(configuration->pinDc, 1);
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    transaction.tx_buffer = data;
    spi_device_polling_transmit(spiDevice, &transaction);
}

void Gdeq031t10Display::waitWhileBusy() const {
    // BUSY pin reads high when the controller is idle/ready.
    while (gpio_get_level(configuration->pinBusy) != 1) {
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void Gdeq031t10Display::reset() const {
    gpio_set_level(configuration->pinReset, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(configuration->pinReset, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void Gdeq031t10Display::initFull() {
    reset();
    writeCommand(CMD_PANEL_SETTING);
    writeData(configuration->mirror180 ? 0x13 : 0x1F);
    writeCommand(CMD_POWER_ON);
    waitWhileBusy();
    panelMode = RefreshMode::Full;
    panelPowerOn = true;
}

void Gdeq031t10Display::initFast() {
    initFull();
    writeCommand(CMD_FAST_MODE_ENABLE);
    writeData(0x02);
    writeCommand(CMD_FAST_MODE_TIMING);
    writeData(0x5A); // ~1.0s
    panelMode = RefreshMode::Fast;
}

void Gdeq031t10Display::initSlow() {
    initFull();
    writeCommand(CMD_FAST_MODE_ENABLE);
    writeData(0x02);
    writeCommand(CMD_FAST_MODE_TIMING);
    writeData(0x6E); // ~1.5s
    panelMode = RefreshMode::Slow;
}

void Gdeq031t10Display::initPartial() {
    initFull();
    writeCommand(CMD_FAST_MODE_ENABLE);
    writeData(0x02);
    writeCommand(CMD_FAST_MODE_TIMING);
    writeData(0x79);
    writeCommand(CMD_VCOM_DATA_INTERVAL);
    writeData(0xD7);
    panelMode = RefreshMode::Partial;
}

void Gdeq031t10Display::ensurePanelReady(RefreshMode mode) {
    if (panelMode != mode) {
        // A mode change needs the full init path: it starts with reset(), which
        // restores register defaults (required when leaving partial mode, whose
        // VCOM/data-interval setting would otherwise linger).
        switch (mode) {
            case RefreshMode::Full: initFull(); break;
            case RefreshMode::Fast: initFast(); break;
            case RefreshMode::Slow: initSlow(); break;
            case RefreshMode::Partial: initPartial(); break;
        }
    } else if (!panelPowerOn) {
        // Registers still hold the mode; only the charge pump was idled.
        writeCommand(CMD_POWER_ON);
        waitWhileBusy();
        panelPowerOn = true;
    }
}

void Gdeq031t10Display::powerOff() {
    writeCommand(CMD_POWER_ON_OFF); // 0x02 standalone = power off
    waitWhileBusy();
    panelPowerOn = false;
}

void Gdeq031t10Display::queueRefresh() {
    xSemaphoreTake(bufferMutex, portMAX_DELAY);
    // renderFramebuffer always holds a complete frame (single buffer, full
    // render mode), so the staged copy is self-contained.
    std::memcpy(pendingFramebuffer.get(), renderFramebuffer.get() + LVGL_I1_PALETTE_SIZE, FRAMEBUFFER_SIZE);
    framePending = true;
    xSemaphoreGive(bufferMutex);
    xTaskNotifyGive(refreshTask);
}

void Gdeq031t10Display::refreshTaskMain(void* parameter) {
    auto* self = static_cast<Gdeq031t10Display*>(parameter);
    self->runRefreshTask();
    xSemaphoreGive(self->refreshTaskExited);
    vTaskDelete(nullptr);
}

void Gdeq031t10Display::runRefreshTask() {
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (refreshTaskShouldExit) {
            return;
        }

        // Drain staged frames latest-wins: everything LVGL flushed while the
        // previous refresh was in progress collapses into one panel update.
        // A frame staged while the panel is off stays staged; the power-on
        // path kicks this task to draw it.
        while (!refreshTaskShouldExit && powered) {
            xSemaphoreTake(bufferMutex, portMAX_DELAY);
            const bool havePending = framePending;
            if (havePending) {
                std::swap(pendingFramebuffer, taskFramebuffer);
                framePending = false;
            }
            xSemaphoreGive(bufferMutex);

            if (!havePending) {
                break;
            }
            refresh();
        }

        // The pipeline went idle: drop the charge pump now rather than on a
        // timer. The mode registers survive a power-off, so the next refresh
        // only pays a short power-on wait (on this task, invisible to the UI).
        xSemaphoreTake(panelMutex, portMAX_DELAY);
        if (initialized && powered && panelPowerOn) {
            powerOff();
        }
        xSemaphoreGive(panelMutex);
    }
}

void Gdeq031t10Display::refresh() {
    const uint8_t* renderBitmap = taskFramebuffer.get();
    constexpr int BYTES_PER_ROW = WIDTH / 8;

    // Find the bounding box of bytes that differ from what the panel holds. The
    // panel stores the inverted (shadow) polarity, so compare against ~render.
    int firstCol = BYTES_PER_ROW, lastCol = -1, firstRow = HEIGHT, lastRow = -1;
    for (int row = 0; row < HEIGHT; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW;
        for (int col = 0; col < BYTES_PER_ROW; col++) {
            if (static_cast<uint8_t>(~renderBitmap[base + col]) != shadowFramebuffer[base + col]) {
                if (col < firstCol) firstCol = col;
                if (col > lastCol) lastCol = col;
                if (row < firstRow) firstRow = row;
                if (row > lastRow) lastRow = row;
            }
        }
    }

    const bool nothingChanged = (lastCol < 0);
    if (nothingChanged && !forceFullRefresh) {
        return; // panel already shows this frame; don't refresh needlessly
    }

    // A change covering most of the screen or an explicit request warrants a
    // full refresh; otherwise refresh just the box.
    const int changedRows = nothingChanged ? 0 : (lastRow - firstRow + 1);
    const bool largeChange = changedRows > (HEIGHT * 3 / 4);

    xSemaphoreTake(panelMutex, portMAX_DELAY);
    if (!powered || !initialized) {
        // The display was turned off/stopped while this frame was staged; the
        // shadow mismatch makes the frame redraw after the next power-on.
        xSemaphoreGive(panelMutex);
        return;
    }
    if (forceFullRefresh || largeChange || nothingChanged) {
        // Explicit requests get the best-quality LUT; automatic ghost-clears use
        // the configured (typically faster) mode.
        refreshFull(forceFullRefresh ? RefreshMode::Full : currentRefreshMode);
        forceFullRefresh = false;
        partialRefreshCount = 0;
        ghostAreaValid = false;
        xSemaphoreGive(panelMutex);
        return;
    }

    // Grow the ghost union with this change so the gate sees where partial
    // updates have accumulated since the last clear.
    if (!ghostAreaValid) {
        ghostFirstCol = firstCol;
        ghostLastCol = lastCol;
        ghostFirstRow = firstRow;
        ghostLastRow = lastRow;
        ghostAreaValid = true;
    } else {
        ghostFirstCol = std::min(ghostFirstCol, firstCol);
        ghostLastCol = std::max(ghostLastCol, lastCol);
        ghostFirstRow = std::min(ghostFirstRow, firstRow);
        ghostLastRow = std::max(ghostLastRow, lastRow);
    }

    if (partialRefreshCount >= MAX_PARTIAL_REFRESHES) {
        // Ghost-clear gate. If the accumulated churn is confined to a small
        // region (a blinking cursor, a line being typed into), scrub just that
        // window — a full-screen flash there is needless and distracting. Only
        // widespread churn earns a whole-panel refresh.
        const int unionWidth = (ghostLastCol - ghostFirstCol + 1) * 8;
        const int unionHeight = ghostLastRow - ghostFirstRow + 1;
        const bool unionIsLarge = unionWidth * unionHeight * 4 >= WIDTH * HEIGHT; // >= 25% of the panel
        if (unionIsLarge) {
            refreshFull(currentRefreshMode);
        } else {
            refreshWindow(ghostFirstCol, ghostLastCol, ghostFirstRow, ghostLastRow, true);
        }
        partialRefreshCount = 0;
        ghostAreaValid = false;
    } else {
        refreshWindow(firstCol, lastCol, firstRow, lastRow, false);
        partialRefreshCount++;
    }
    xSemaphoreGive(panelMutex);
}

void Gdeq031t10Display::refreshFull(RefreshMode mode) {
    ensurePanelReady(mode);

    const uint8_t* renderBitmap = taskFramebuffer.get();

    // shadowFramebuffer keeps the panel-polarity copy of the last frame for the
    // controller's old/new differential refresh; LVGL's I1 polarity is inverted.
    writeCommand(CMD_DATA_START_OLD);
    writeData(shadowFramebuffer.get(), FRAMEBUFFER_SIZE);

    for (size_t i = 0; i < FRAMEBUFFER_SIZE; i++) {
        shadowFramebuffer[i] = static_cast<uint8_t>(~renderBitmap[i]);
    }
    writeCommand(CMD_DATA_START_NEW);
    writeData(shadowFramebuffer.get(), FRAMEBUFFER_SIZE);

    writeCommand(CMD_DISPLAY_REFRESH);
    vTaskDelay(pdMS_TO_TICKS(1)); // datasheet requires >=200us settle before polling BUSY
    waitWhileBusy();
}

void Gdeq031t10Display::refreshWindow(int firstByteCol, int lastByteCol, int firstRow, int lastRow, bool scrubGhosts) {
    // Partial LUT (fast waveform via temperature force) on a sub-region only. The
    // RAM window must be byte-aligned in X, which it already is (byte columns).
    ensurePanelReady(RefreshMode::Partial);

    const uint8_t* renderBitmap = taskFramebuffer.get();
    constexpr int BYTES_PER_ROW = WIDTH / 8;
    const int widthBytes = lastByteCol - firstByteCol + 1;

    const uint16_t x = static_cast<uint16_t>(firstByteCol * 8);
    const uint16_t xe = static_cast<uint16_t>(lastByteCol * 8 + 7);
    const uint16_t y = static_cast<uint16_t>(firstRow);
    const uint16_t ye = static_cast<uint16_t>(lastRow);

    // Set the partial RAM window (GxEPD2 GDEQ031T10 sequence).
    writeCommand(CMD_PARTIAL_IN);
    writeCommand(CMD_PARTIAL_WINDOW);
    writeData(static_cast<uint8_t>(x));
    writeData(static_cast<uint8_t>(xe));
    writeData(static_cast<uint8_t>(y >> 8));
    writeData(static_cast<uint8_t>(y & 0xFF));
    writeData(static_cast<uint8_t>(ye >> 8));
    writeData(static_cast<uint8_t>(ye & 0xFF));
    writeData(0x01);

    // Old region: normally the region's current panel contents (shadow),
    // gathered contiguously. For a ghost scrub, feed the complement of the new
    // data instead: the controller then sees every pixel as changed and drives
    // each one through a transition, clearing accumulated partial-refresh
    // ghosting in this window only.
    size_t n = 0;
    for (int row = firstRow; row <= lastRow; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW + firstByteCol;
        if (scrubGhosts) {
            // New panel data is ~renderBitmap, so its complement is renderBitmap.
            std::memcpy(&regionBuffer[n], &renderBitmap[base], widthBytes);
        } else {
            std::memcpy(&regionBuffer[n], &shadowFramebuffer[base], widthBytes);
        }
        n += widthBytes;
    }
    writeCommand(CMD_DATA_START_OLD);
    writeData(regionBuffer.get(), n);

    // New region: inverted render, and update the shadow for this region as we go.
    n = 0;
    for (int row = firstRow; row <= lastRow; row++) {
        const size_t base = static_cast<size_t>(row) * BYTES_PER_ROW + firstByteCol;
        for (int c = 0; c < widthBytes; c++) {
            const uint8_t value = static_cast<uint8_t>(~renderBitmap[base + c]);
            regionBuffer[n++] = value;
            shadowFramebuffer[base + c] = value;
        }
    }
    writeCommand(CMD_DATA_START_NEW);
    writeData(regionBuffer.get(), n);

    writeCommand(CMD_DISPLAY_REFRESH);
    vTaskDelay(pdMS_TO_TICKS(1));
    waitWhileBusy();
    writeCommand(CMD_PARTIAL_OUT);
}

void Gdeq031t10Display::flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap) {
    // pixelMap points into renderFramebuffer (it was passed directly to
    // lv_display_set_buffers). Only stage the frame here: the physical refresh
    // takes up to ~1s, so it runs on the driver's refresh task instead of
    // blocking the LVGL task (which would stall all input handling).
    auto* self = static_cast<Gdeq031t10Display*>(lv_display_get_user_data(display));

    if (lv_display_flush_is_last(display)) {
        self->queueRefresh();
    }

    lv_display_flush_ready(display);
}

void Gdeq031t10Display::themeApplyCallback(lv_theme_t* /*theme*/, lv_obj_t* obj) {
    // Keep textarea cursors solid (no blink): on e-paper each blink toggle is a
    // panel refresh. anim_duration 0 makes lv_textarea skip the blink animation.
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_set_style_anim_duration(obj, 0, LV_PART_CURSOR);
    }
}

bool Gdeq031t10Display::start() {
    if (initialized) {
        return true;
    }

    gpio_config_t outputConfig = {
        .pin_bit_mask = (1ULL << configuration->pinDc) | (1ULL << configuration->pinReset),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&outputConfig);

    gpio_config_t busyConfig = {
        .pin_bit_mask = 1ULL << configuration->pinBusy,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busyConfig);

    spi_device_interface_config_t deviceConfig = {
        .mode = 0,
        .clock_speed_hz = configuration->clockSpeedHz,
        .spics_io_num = configuration->pinCs,
        .queue_size = 1,
    };

    if (spi_bus_add_device(configuration->spiHost, &deviceConfig, &spiDevice) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device");
        return false;
    }

    if (panelMutex == nullptr) {
        panelMutex = xSemaphoreCreateMutex();
    }
    if (bufferMutex == nullptr) {
        bufferMutex = xSemaphoreCreateMutex();
    }
    if (refreshTaskExited == nullptr) {
        refreshTaskExited = xSemaphoreCreateBinary();
    }

    shadowFramebuffer = std::make_unique<uint8_t[]>(FRAMEBUFFER_SIZE);
    // LVGL needs room for the 8-byte I1 palette ahead of the 1bpp bitmap.
    renderFramebuffer = std::make_unique<uint8_t[]>(LVGL_I1_PALETTE_SIZE + FRAMEBUFFER_SIZE);
    // Scratch for gathering a windowed region (worst case = the whole frame).
    regionBuffer = std::make_unique<uint8_t[]>(FRAMEBUFFER_SIZE);
    pendingFramebuffer = std::make_unique<uint8_t[]>(FRAMEBUFFER_SIZE);
    taskFramebuffer = std::make_unique<uint8_t[]>(FRAMEBUFFER_SIZE);
    std::memset(shadowFramebuffer.get(), 0xFF, FRAMEBUFFER_SIZE);
    std::memset(renderFramebuffer.get(), 0xFF, LVGL_I1_PALETTE_SIZE + FRAMEBUFFER_SIZE);
    std::memset(pendingFramebuffer.get(), 0xFF, FRAMEBUFFER_SIZE);
    std::memset(taskFramebuffer.get(), 0xFF, FRAMEBUFFER_SIZE);

    currentRefreshMode = configuration->defaultRefreshMode;
    initFull();
    powered = true;
    initialized = true;

    refreshTaskShouldExit = false;
    if (xTaskCreate(&refreshTaskMain, "epd_refresh", REFRESH_TASK_STACK_SIZE, this, REFRESH_TASK_PRIORITY, &refreshTask) != pdPASS) {
        LOG_E(TAG, "Failed to create refresh task");
        initialized = false;
        return false;
    }
    return true;
}

bool Gdeq031t10Display::stop() {
    if (!initialized) {
        return true;
    }

    stopLvgl();

    // Join the refresh task before touching the panel: it may be mid-refresh.
    refreshTaskShouldExit = true;
    xTaskNotifyGive(refreshTask);
    xSemaphoreTake(refreshTaskExited, portMAX_DELAY);
    refreshTask = nullptr;

    setPowerOn(false);

    xSemaphoreTake(panelMutex, portMAX_DELAY);
    initialized = false;
    spi_bus_remove_device(spiDevice);
    spiDevice = nullptr;
    shadowFramebuffer.reset();
    renderFramebuffer.reset();
    regionBuffer.reset();
    pendingFramebuffer.reset();
    taskFramebuffer.reset();
    framePending = false;
    xSemaphoreGive(panelMutex);
    return true;
}

void Gdeq031t10Display::setPowerOn(bool turnOn) {
    if (turnOn == powered) {
        return;
    }

    xSemaphoreTake(panelMutex, portMAX_DELAY);
    if (turnOn) {
        initFull(); // toggling RST also wakes the panel from deep sleep
    } else {
        if (panelPowerOn) {
            powerOff();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        writeCommand(CMD_DEEP_SLEEP);
        writeData(DEEP_SLEEP_CHECK_CODE);
        // Deep sleep needs a reset to wake, which restores register defaults.
        panelMode.reset();
    }

    powered = turnOn;
    xSemaphoreGive(panelMutex);

    if (turnOn && refreshTask != nullptr) {
        // initFull left the charge pump on. Kick the refresh task: it redraws
        // any frame staged while the panel was off, then idles the pump down.
        xTaskNotifyGive(refreshTask);
    }
}

void Gdeq031t10Display::requestFullRefresh() {
    forceFullRefresh = true;
}

bool Gdeq031t10Display::startLvgl() {
    if (lvglDisplay != nullptr) {
        return true;
    }

    lvglDisplay = lv_display_create(Gdeq031t10Display::WIDTH, Gdeq031t10Display::HEIGHT);
    if (lvglDisplay == nullptr) {
        return false;
    }

    lv_display_set_user_data(lvglDisplay, this);
    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_I1);

    // The default colour theme renders accent-coloured text/icons that threshold
    // to near-invisible on a 1bpp panel. Apply LVGL's monochrome theme (light
    // background, dark foreground) so UI content shows as solid black on white.
    lv_theme_t* baseTheme = lv_theme_mono_init(lvglDisplay, false, LV_FONT_DEFAULT);
    // Chain a theme on top of the mono theme that disables the textarea cursor
    // blink. A blinking cursor invalidates its region ~twice a second, and on
    // e-paper every invalidation is a panel refresh, so text-entry screens (e.g.
    // the Wi-Fi password field) flash continuously. The mono theme still applies
    // first (it's the parent); themeApplyCallback only pins the cursor solid.
    static lv_theme_t epaperTheme;
    epaperTheme = *baseTheme;
    lv_theme_set_parent(&epaperTheme, baseTheme);
    lv_theme_set_apply_cb(&epaperTheme, &Gdeq031t10Display::themeApplyCallback);
    lv_display_set_theme(lvglDisplay, &epaperTheme);
    lv_display_set_render_mode(lvglDisplay, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_buffers(lvglDisplay, renderFramebuffer.get(), nullptr, LVGL_I1_PALETTE_SIZE + FRAMEBUFFER_SIZE, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(lvglDisplay, flushCallback);

    return true;
}

bool Gdeq031t10Display::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return true;
    }

    lv_display_delete(lvglDisplay);
    lvglDisplay = nullptr;
    return true;
}
