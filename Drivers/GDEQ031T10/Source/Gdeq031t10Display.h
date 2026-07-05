#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>

#include <atomic>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <memory>
#include <optional>

/**
 * Driver for the GoodDisplay GDEQ031T10 (UC8253-family controller) 3.1" 320x240
 * SPI e-paper panel, as used on the LilyGO T-Deck Pro/Max.
 *
 * Command sequence and timings are ported from the vendor reference driver in
 * Xinyuan-LilyGO/T-Deck-MAX (examples/Elink_paper/GDEQ031T10_Arduino).
 */
class Gdeq031t10Display final : public tt::hal::display::DisplayDevice {

public:

    enum class RefreshMode {
        Full,    // ~3s, best quality
        Fast,    // ~1.0s
        Slow,    // ~1.5s, fast LUT with extra settling
        Partial  // ~0.5s, full-frame partial-mode refresh (more ghosting)
    };

    class Configuration {
    public:

        Configuration(
            spi_host_device_t spiHost,
            gpio_num_t pinCs,
            gpio_num_t pinDc,
            gpio_num_t pinReset,
            gpio_num_t pinBusy,
            std::shared_ptr<tt::hal::touch::TouchDevice> touch = nullptr,
            int clockSpeedHz = 4'000'000,
            RefreshMode defaultRefreshMode = RefreshMode::Full,
            bool mirror180 = false
        ) : spiHost(spiHost),
            pinCs(pinCs),
            pinDc(pinDc),
            pinReset(pinReset),
            pinBusy(pinBusy),
            touch(std::move(touch)),
            clockSpeedHz(clockSpeedHz),
            defaultRefreshMode(defaultRefreshMode),
            mirror180(mirror180)
        {}

        spi_host_device_t spiHost;
        gpio_num_t pinCs;
        gpio_num_t pinDc;
        gpio_num_t pinReset;
        gpio_num_t pinBusy;
        std::shared_ptr<tt::hal::touch::TouchDevice> touch;
        int clockSpeedHz;
        RefreshMode defaultRefreshMode;
        /** Panel is mounted upside down relative to the reference orientation */
        bool mirror180;
    };

    static constexpr uint16_t WIDTH = 240;
    static constexpr uint16_t HEIGHT = 320;
    static constexpr size_t FRAMEBUFFER_SIZE = (WIDTH * HEIGHT) / 8; // 1 bpp packed
    // LVGL 9 stores a 2-colour palette (2 x lv_color32_t = 8 bytes) at the start
    // of an LV_COLOR_FORMAT_I1 buffer, before the packed 1bpp bitmap.
    static constexpr size_t LVGL_I1_PALETTE_SIZE = 8;

private:

    std::unique_ptr<Configuration> configuration;
    spi_device_handle_t spiDevice = nullptr;
    lv_display_t* _Nullable lvglDisplay = nullptr;
    /** Mirrors what the panel currently holds, required by the controller's
     * "old data" + "new data" double-buffered refresh protocol. */
    std::unique_ptr<uint8_t[]> shadowFramebuffer;
    /** Render target for LVGL; copied to the panel on flush */
    std::unique_ptr<uint8_t[]> renderFramebuffer;
    bool initialized = false;
    bool powered = false;
    RefreshMode currentRefreshMode = RefreshMode::Full;
    /** Number of windowed partial refreshes since the last full refresh. A full
     * refresh is forced once this reaches MAX_PARTIAL_REFRESHES to clear the
     * ghosting that partial updates accumulate. */
    uint8_t partialRefreshCount = 0;
    /** Forces the next refresh to be a full-screen refresh (set at boot and by
     * requestFullRefresh, read on the refresh task). */
    std::atomic<bool> forceFullRefresh = true;
    /** Scratch buffer used to gather a windowed region's bytes for one SPI write. */
    std::unique_ptr<uint8_t[]> regionBuffer;
    static constexpr uint8_t MAX_PARTIAL_REFRESHES = 8;
    /** Union bounding box (byte-column/row space) of all windowed partial
     * refreshes since the last ghost-clear. When the partial-refresh gate
     * expires, this decides between a localized scrub and a full-screen
     * refresh: small repeated updates (a text cursor, a typing line) shouldn't
     * flash the whole panel. */
    bool ghostAreaValid = false;
    int ghostFirstCol = 0;
    int ghostLastCol = 0;
    int ghostFirstRow = 0;
    int ghostLastRow = 0;
    /** Waveform mode the panel registers currently hold. Empty when the
     * registers are in an unknown state (before first init, or after deep sleep,
     * which requires a reset that restores defaults). */
    std::optional<RefreshMode> panelMode;
    /** True while the panel's charge pump is on (CMD_POWER_ON issued, no
     * power-off since). */
    bool panelPowerOn = false;
    /** Serializes panel/SPI access between the refresh task and external
     * callers (setPowerOn, stop). */
    SemaphoreHandle_t panelMutex = nullptr;

    // The physical refresh takes hundreds of ms to over a second of ink
    // movement. Running it inside LVGL's flush callback would block the LVGL
    // task (and with it all input handling) for that long, so flushes only
    // stage the frame and a dedicated task drives the panel. Frames coalesce
    // latest-wins: however many flushes arrive during a refresh, only the
    // newest staged frame is drawn next.
    static constexpr uint32_t REFRESH_TASK_PRIORITY = 3; // below LVGL (6), above idle
    static constexpr uint32_t REFRESH_TASK_STACK_SIZE = 4096;
    TaskHandle_t refreshTask = nullptr;
    /** Signalled by the refresh task right before it exits; stop() joins on it. */
    SemaphoreHandle_t refreshTaskExited = nullptr;
    /** Guards pendingFramebuffer and framePending (LVGL flush vs refresh task). */
    SemaphoreHandle_t bufferMutex = nullptr;
    /** Latest complete frame staged by the LVGL flush callback. */
    std::unique_ptr<uint8_t[]> pendingFramebuffer;
    /** The refresh task's working copy of the frame it is drawing (swapped with
     * pendingFramebuffer under bufferMutex; no copy on the task side). */
    std::unique_ptr<uint8_t[]> taskFramebuffer;
    bool framePending = false;
    std::atomic<bool> refreshTaskShouldExit = false;

    void writeCommand(uint8_t command);
    void writeData(const uint8_t* data, size_t length);
    void writeData(uint8_t data) { writeData(&data, 1); }
    void waitWhileBusy() const;
    void reset() const;

    void initFull();
    void initFast();
    void initSlow();
    void initPartial();

    /** Puts the panel in the given waveform mode with the charge pump on,
     * skipping the (expensive) init sequence when it already is. */
    void ensurePanelReady(RefreshMode mode);

    void powerOff();
    /** Stages the LVGL-rendered frame for the refresh task (called on flush). */
    void queueRefresh();
    static void refreshTaskMain(void* parameter);
    void runRefreshTask();
    void refresh();
    void refreshFull(RefreshMode mode);
    /** Windowed partial refresh. With scrubGhosts the controller is fed "old"
     * data that is the complement of the new frame, so it drives every pixel in
     * the window through a transition — a localized ghost-clear that only
     * flashes the window itself. */
    void refreshWindow(int firstByteCol, int lastByteCol, int firstRow, int lastRow, bool scrubGhosts);

    static void flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap);
    /** Theme hook: disables the textarea cursor blink (see startLvgl). */
    static void themeApplyCallback(lv_theme_t* theme, lv_obj_t* obj);

public:

    explicit Gdeq031t10Display(std::unique_ptr<Configuration> inConfiguration) : configuration(std::move(inConfiguration)) {
        assert(configuration != nullptr);
    }

    ~Gdeq031t10Display() override {
        if (panelMutex != nullptr) {
            vSemaphoreDelete(panelMutex);
        }
        if (bufferMutex != nullptr) {
            vSemaphoreDelete(bufferMutex);
        }
        if (refreshTaskExited != nullptr) {
            vSemaphoreDelete(refreshTaskExited);
        }
    }

    std::string getName() const override { return "GDEQ031T10"; }

    std::string getDescription() const override { return "GoodDisplay GDEQ031T10 e-paper display"; }

    bool start() override;
    bool stop() override;

    void setPowerOn(bool turnOn) override;
    bool isPoweredOn() const override { return powered; }
    bool supportsPowerControl() const override { return true; }

    void requestFullRefresh() override;

    std::shared_ptr<tt::hal::touch::TouchDevice> _Nullable getTouchDevice() override {
        return configuration->touch;
    }

    bool supportsLvgl() const override { return true; }
    bool startLvgl() override;
    bool stopLvgl() override;
    lv_display_t* _Nullable getLvglDisplay() const override { return lvglDisplay; }

    bool supportsDisplayDriver() const override { return false; }
    std::shared_ptr<tt::hal::display::DisplayDriver> _Nullable getDisplayDriver() override { return nullptr; }

    /** Force the next flush to use a specific refresh mode (one-shot) */
    void setRefreshMode(RefreshMode mode) { currentRefreshMode = mode; }
};
