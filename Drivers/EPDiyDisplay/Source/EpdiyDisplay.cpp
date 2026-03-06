#include "EpdiyDisplay.h"

#include <tactility/check.h>
#include <tactility/log.h>

#include <esp_heap_caps.h>
#include <esp_timer.h>

constexpr const char* TAG = "EpdiyDisplay";

bool EpdiyDisplay::s_hlInitialized = false;
EpdiyHighlevelState EpdiyDisplay::s_hlState = {};

EpdiyDisplay::EpdiyDisplay(std::unique_ptr<Configuration> inConfiguration)
    : configuration(std::move(inConfiguration)) {
    check(configuration != nullptr);
    check(configuration->board != nullptr);
    check(configuration->display != nullptr);
}

EpdiyDisplay::~EpdiyDisplay() {
    if (lvglDisplay != nullptr) {
        stopLvgl();
    }
    if (initialized) {
        stop();
    }
}

bool EpdiyDisplay::start() {
    if (initialized) {
        LOG_W(TAG, "Already initialized");
        return true;
    }

    // Initialize EPDiy low-level hardware
    epd_init(
        configuration->board,
        configuration->display,
        configuration->initOptions
    );

    // Set rotation BEFORE initializing highlevel state
    epd_set_rotation(configuration->rotation);
    LOG_I(TAG, "Display rotation set to %d", configuration->rotation);

    // Initialize the high-level API only once — epd_hl_init() sets a static flag internally
    // and there is no matching epd_hl_deinit(). Reuse the existing state on subsequent starts.
    if (!s_hlInitialized) {
        s_hlState = epd_hl_init(configuration->waveform);
        if (s_hlState.front_fb == nullptr) {
            LOG_E(TAG, "Failed to initialize EPDiy highlevel state");
            epd_deinit();
            return false;
        }
        s_hlInitialized = true;
        LOG_I(TAG, "EPDiy highlevel state initialized");
    } else {
        LOG_I(TAG, "Reusing existing EPDiy highlevel state");
    }

    highlevelState = s_hlState;
    framebuffer = epd_hl_get_framebuffer(&highlevelState);

    initialized = true;
    LOG_I(TAG, "EPDiy initialized successfully (%dx%d native, %dx%d rotated)", epd_width(), epd_height(), epd_rotated_display_width(), epd_rotated_display_height());

    // Perform initial clear to ensure clean state
    LOG_I(TAG, "Performing initial screen clear...");
    clearScreen();
    LOG_I(TAG, "Screen cleared");

    return true;
}

bool EpdiyDisplay::stop() {
    if (!initialized) {
        return true;
    }

    if (lvglDisplay != nullptr) {
        stopLvgl();
    }

    // Power off the display
    if (powered) {
        setPowerOn(false);
    }

    // Deinitialize EPDiy low-level hardware.
    // The HL framebuffers (s_hlState) are intentionally kept alive: epd_hl_init() has no
    // matching deinit and sets an internal already_initialized flag, so the HL state must
    // persist across stop()/start() cycles and be reused on the next start().
    epd_deinit();

    // Clear instance references to HL state (the static s_hlState still owns the memory)
    highlevelState = {};
    framebuffer = nullptr;

    initialized = false;
    LOG_I(TAG, "EPDiy deinitialized (HL state preserved for restart)");

    return true;
}

void EpdiyDisplay::setPowerOn(bool turnOn) {
    if (!initialized) {
        LOG_W(TAG, "Cannot change power state - EPD not initialized");
        return;
    }

    if (powered == turnOn) {
        return;
    }

    if (turnOn) {
        epd_poweron();
        powered = true;
        LOG_D(TAG, "EPD power on");
    } else {
        epd_poweroff();
        powered = false;
        LOG_D(TAG, "EPD power off");
    }
}

// LVGL functions
bool EpdiyDisplay::startLvgl() {
    if (lvglDisplay != nullptr) {
        LOG_W(TAG, "LVGL already initialized");
        return true;
    }

    if (!initialized) {
        LOG_E(TAG, "EPD not initialized, call start() first");
        return false;
    }

    // Get the native display dimensions
    uint16_t width = epd_width();
    uint16_t height = epd_height();

    LOG_I(TAG, "Creating LVGL display: %dx%d (EPDiy rotation: %d)", width, height, configuration->rotation);

    // Create LVGL display with native dimensions
    lvglDisplay = lv_display_create(width, height);
    if (lvglDisplay == nullptr) {
        LOG_E(TAG, "Failed to create LVGL display");
        return false;
    }

    // EPD uses 4-bit grayscale (16 levels)
    // Map to LVGL's L8 format (8-bit grayscale)
    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_L8);
    auto lv_rotation = epdRotationToLvgl(configuration->rotation);
    lv_display_set_rotation(lvglDisplay, lv_rotation);

    // Allocate LVGL draw buffer (L8 format: 1 byte per pixel)
    size_t draw_buffer_size = static_cast<size_t>(width) * height;

    lvglDrawBuffer = static_cast<uint8_t*>(heap_caps_malloc(draw_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (lvglDrawBuffer == nullptr) {
        LOG_W(TAG, "PSRAM allocation failed for draw buffer, falling back to internal memory");
        lvglDrawBuffer = static_cast<uint8_t*>(heap_caps_malloc(draw_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    }

    if (lvglDrawBuffer == nullptr) {
        LOG_E(TAG, "Failed to allocate draw buffer");
        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
        return false;
    }

    // Pre-allocate 4-bit packed pixel buffer used in flushInternal (avoids per-flush heap allocation)
    // Row stride with odd-width padding: (width + 1) / 2 bytes per row
    size_t packed_buffer_size = static_cast<size_t>((width + 1) / 2) * static_cast<size_t>(height);
    packedBuffer = static_cast<uint8_t*>(heap_caps_malloc(packed_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (packedBuffer == nullptr) {
        LOG_W(TAG, "PSRAM allocation failed for packed buffer, falling back to internal memory");
        packedBuffer = static_cast<uint8_t*>(heap_caps_malloc(packed_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    }

    if (packedBuffer == nullptr) {
        LOG_E(TAG, "Failed to allocate packed pixel buffer");
        heap_caps_free(lvglDrawBuffer);
        lvglDrawBuffer = nullptr;
        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
        return false;
    }

    // For EPD, we want full refresh mode based on configuration
    lv_display_render_mode_t render_mode = configuration->fullRefresh
        ? LV_DISPLAY_RENDER_MODE_FULL
        : LV_DISPLAY_RENDER_MODE_PARTIAL;

    lv_display_set_buffers(lvglDisplay, lvglDrawBuffer, NULL, draw_buffer_size, render_mode);

    // Set flush callback
    lv_display_set_flush_cb(lvglDisplay, flushCallback);
    lv_display_set_user_data(lvglDisplay, this);

    // Register rotation change event callback
    lv_display_add_event_cb(lvglDisplay, rotationEventCallback, LV_EVENT_RESOLUTION_CHANGED, this);
    LOG_D(TAG, "Registered rotation change event callback");

    // Start touch device if present
    auto touch_device = getTouchDevice();
    if (touch_device != nullptr && touch_device->supportsLvgl()) {
        LOG_D(TAG, "Starting touch device for LVGL");
        if (!touch_device->startLvgl(lvglDisplay)) {
            LOG_W(TAG, "Failed to start touch device for LVGL");
        }
    }

    LOG_I(TAG, "LVGL display initialized");
    return true;
}

bool EpdiyDisplay::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return true;
    }

    LOG_I(TAG, "Stopping LVGL display");

    // Stop touch device
    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->stopLvgl();
    }

    if (lvglDrawBuffer != nullptr) {
        heap_caps_free(lvglDrawBuffer);
        lvglDrawBuffer = nullptr;
    }

    if (packedBuffer != nullptr) {
        heap_caps_free(packedBuffer);
        packedBuffer = nullptr;
    }

    // Delete the LVGL display object
    lv_display_delete(lvglDisplay);
    lvglDisplay = nullptr;


    LOG_I(TAG, "LVGL display stopped");
    return true;
}

void EpdiyDisplay::flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap) {
    auto* instance = static_cast<EpdiyDisplay*>(lv_display_get_user_data(display));
    if (instance != nullptr) {
        uint64_t t0 = esp_timer_get_time();
        const bool isLast = lv_display_flush_is_last(display);
        instance->flushInternal(area, pixelMap, isLast);
        LOG_D(TAG, "flush took %llu us", (unsigned long long)(esp_timer_get_time() - t0));
    } else {
        LOG_W(TAG, "flush callback called with null instance");
    }
    lv_display_flush_ready(display);
}


// EPD functions
void EpdiyDisplay::clearScreen() {
    if (!initialized) {
        LOG_E(TAG, "EPD not initialized");
        return;
    }

    if (!powered) {
        setPowerOn(true);
    }

    epd_clear();

    // Also clear the framebuffer
    epd_hl_set_all_white(&highlevelState);
}

void EpdiyDisplay::clearArea(EpdRect area) {
    if (!initialized) {
        LOG_E(TAG, "EPD not initialized");
        return;
    }

    if (!powered) {
        setPowerOn(true);
    }

    epd_clear_area(area);
}

enum EpdDrawError EpdiyDisplay::updateScreen(enum EpdDrawMode mode, int temperature) {
    if (!initialized) {
        LOG_E(TAG, "EPD not initialized");
        return EPD_DRAW_FAILED_ALLOC;
    }

    if (!powered) {
        setPowerOn(true);
    }

    // Use defaults if not specified
    if (mode == MODE_UNKNOWN_WAVEFORM) {
        mode = configuration->defaultDrawMode;
    }
    if (temperature == -1) {
        temperature = configuration->defaultTemperature;
    }

    return epd_hl_update_screen(&highlevelState, mode, temperature);
}

enum EpdDrawError EpdiyDisplay::updateArea(EpdRect area, enum EpdDrawMode mode, int temperature) {
    if (!initialized) {
        LOG_E(TAG, "EPD not initialized");
        return EPD_DRAW_FAILED_ALLOC;
    }

    if (!powered) {
        setPowerOn(true);
    }

    // Use defaults if not specified
    if (mode == MODE_UNKNOWN_WAVEFORM) {
        mode = configuration->defaultDrawMode;
    }
    if (temperature == -1) {
        temperature = configuration->defaultTemperature;
    }

    return epd_hl_update_area(&highlevelState, mode, temperature, area);
}

void EpdiyDisplay::setAllWhite() {
    if (!initialized) {
        LOG_E(TAG, "EPD not initialized");
        return;
    }

    epd_hl_set_all_white(&highlevelState);
}

// Internal functions
void EpdiyDisplay::flushInternal(const lv_area_t* area, uint8_t* pixelMap, bool isLast) {
    if (!initialized) {
        LOG_E(TAG, "Cannot flush - EPD not initialized");
        return;
    }

    if (!powered) {
        setPowerOn(true);
    }

    const int x = area->x1;
    const int y = area->y1;
    const int width = lv_area_get_width(area);
    const int height = lv_area_get_height(area);

    LOG_D(TAG, "Flushing area: x=%d, y=%d, w=%d, h=%d isLast=%d", x, y, width, height, (int)isLast);

    // Convert L8 (8-bit grayscale, 0=black/255=white) to EPDiy 4-bit (0=black/15=white).
    // Pack 2 pixels per byte: lower nibble = even column, upper nibble = odd column.
    // Row stride includes one padding nibble for odd widths to keep rows aligned.
    // Threshold at 128 (matching FastEPD BB_MODE_1BPP): pixels > 127 → full white (15),
    // pixels ≤ 127 → full black (0).  Maximum contrast for the Mono theme and correct for
    // MODE_DU which only drives two levels.  For greyscale content / MODE_GL16, replace
    // the threshold with `src[col] >> 4` to preserve intermediate grey levels.
    const int row_stride = (width + 1) / 2;
    for (int row = 0; row < height; ++row) {
        const uint8_t* src = pixelMap + static_cast<size_t>(row) * width;
        uint8_t* dst = packedBuffer + static_cast<size_t>(row) * row_stride;
        for (int col = 0; col < width; col += 2) {
            const uint8_t p0 = (src[col] > 127) ? 15u : 0u;
            const uint8_t p1 = (col + 1 < width) ? ((src[col + 1] > 127) ? 15u : 0u) : 0u;
            dst[col / 2] = static_cast<uint8_t>((p1 << 4) | p0);
        }
    }

    const EpdRect update_area = {
        .x = x,
        .y = y,
        .width = static_cast<uint16_t>(width),
        .height = static_cast<uint16_t>(height)
    };

    // Write pixels into EPDiy's framebuffer (no hardware I/O, just memory)
    epd_draw_rotated_image(update_area, packedBuffer, framebuffer);

    // Only trigger EPD hardware update on the last flush of this render cycle.
    // EPDiy's epd_prep tasks run at configMAX_PRIORITIES-1 with busy-wait loops; calling
    // epd_hl_update_area on every partial flush starves IDLE and triggers the task watchdog
    // during scroll animations. Batching to one hardware update per LVGL render cycle fixes this.
    if (isLast) {
        epd_hl_update_screen(
            &highlevelState,
            static_cast<EpdDrawMode>(configuration->defaultDrawMode | MODE_PACKING_2PPB),
            configuration->defaultTemperature
        );
    }
}

lv_display_rotation_t EpdiyDisplay::epdRotationToLvgl(enum EpdRotation epdRotation) {
    // Static lookup table for EPD -> LVGL rotation mapping
    // EPDiy: LANDSCAPE = 0°, PORTRAIT = 90° CW, INVERTED_LANDSCAPE = 180°, INVERTED_PORTRAIT = 270° CW
    // LVGL: 0 = 0°, 90 = 90° CW, 180 = 180°, 270 = 270° CW
    static const lv_display_rotation_t rotationMap[] = {
        LV_DISPLAY_ROTATION_0, // EPD_ROT_LANDSCAPE (0)
        LV_DISPLAY_ROTATION_270, // EPD_ROT_PORTRAIT (1) - 90° CW in EPD is 270° in LVGL
        LV_DISPLAY_ROTATION_180, // EPD_ROT_INVERTED_LANDSCAPE (2)
        LV_DISPLAY_ROTATION_90 // EPD_ROT_INVERTED_PORTRAIT (3) - 270° CW in EPD is 90° in LVGL
    };

    // Validate input and return mapped value
    if (epdRotation >= 0 && epdRotation < 4) {
        return rotationMap[epdRotation];
    }

    // Default to landscape if invalid
    return LV_DISPLAY_ROTATION_0;
}

enum EpdRotation EpdiyDisplay::lvglRotationToEpd(lv_display_rotation_t lvglRotation) {
    // Static lookup table for LVGL -> EPD rotation mapping
    static const enum EpdRotation rotationMap[] = {
        EPD_ROT_LANDSCAPE, // LV_DISPLAY_ROTATION_0 (0)
        EPD_ROT_INVERTED_PORTRAIT, // LV_DISPLAY_ROTATION_90 (1)
        EPD_ROT_INVERTED_LANDSCAPE, // LV_DISPLAY_ROTATION_180 (2)
        EPD_ROT_PORTRAIT // LV_DISPLAY_ROTATION_270 (3)
    };

    // Validate input and return mapped value
    if (lvglRotation >= LV_DISPLAY_ROTATION_0 && lvglRotation <= LV_DISPLAY_ROTATION_270) {
        return rotationMap[lvglRotation];
    }

    // Default to landscape if invalid
    return EPD_ROT_LANDSCAPE;
}

void EpdiyDisplay::rotationEventCallback(lv_event_t* event) {
    auto* display = static_cast<EpdiyDisplay*>(lv_event_get_user_data(event));
    if (display == nullptr) {
        return;
    }

    lv_display_t* lvgl_display = static_cast<lv_display_t*>(lv_event_get_target(event));
    if (lvgl_display == nullptr) {
        return;
    }

    lv_display_rotation_t rotation = lv_display_get_rotation(lvgl_display);
    display->handleRotationChange(rotation);
}

void EpdiyDisplay::handleRotationChange(lv_display_rotation_t lvgl_rotation) {
    // Map LVGL rotation to EPDiy rotation using lookup table
    enum EpdRotation epd_rotation = lvglRotationToEpd(lvgl_rotation);

    // Update EPDiy rotation
    LOG_I(TAG, "LVGL rotation changed to %d, setting EPDiy rotation to %d", lvgl_rotation, epd_rotation);
    epd_set_rotation(epd_rotation);

    // Update configuration to keep it in sync
    configuration->rotation = epd_rotation;

    // Log the new dimensions
    LOG_I(TAG, "Display dimensions after rotation: %dx%d", epd_rotated_display_width(), epd_rotated_display_height());
}
