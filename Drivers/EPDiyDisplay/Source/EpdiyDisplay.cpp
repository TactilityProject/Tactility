#include "EpdiyDisplay.h"

#include <Tactility/Check.h>
#include <Tactility/LogEsp.h>

#include <cstring>
#include <esp_heap_caps.h>
#include <esp_timer.h>

constexpr const char* TAG = "EpdiyDisplay";

EpdiyDisplay::EpdiyDisplay(std::unique_ptr<Configuration> inConfiguration)
    : configuration(std::move(inConfiguration)),
      lock(std::make_shared<tt::Mutex>())
{
    tt_check(configuration != nullptr);
    tt_check(configuration->board != nullptr);
    tt_check(configuration->display != nullptr);
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
        TT_LOG_W(TAG, "Already initialized");
        return true;
    }
    
    // Initialize EPDiy
    epd_init(
        configuration->board, 
        configuration->display, 
        configuration->initOptions
    );

    // Set rotation BEFORE initializing highlevel state
    epd_set_rotation(configuration->rotation);
    TT_LOG_I(TAG, "Display rotation set to %d", configuration->rotation);

    // Initialize the high-level API
    highlevelState = epd_hl_init(configuration->waveform);
    if (highlevelState.front_fb == nullptr) {
        TT_LOG_E(TAG, "Failed to initialize EPDiy highlevel state");
        epd_deinit();
        return false;
    }

    framebuffer = epd_hl_get_framebuffer(&highlevelState);

    initialized = true;
    TT_LOG_I(TAG, "EPDiy initialized successfully (%dx%d native, %dx%d rotated)", 
             epd_width(), epd_height(),
             epd_rotated_display_width(), epd_rotated_display_height());

    // Perform initial clear to ensure clean state
    TT_LOG_I(TAG, "Performing initial screen clear...");
    clearScreen();
    TT_LOG_I(TAG, "Screen cleared");

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

    // Note: EPDiy highlevel state buffers are allocated in PSRAM
    // and will be freed when the system shuts down
    // We don't have an explicit cleanup function for highlevel state

    // Deinitialize EPDiy
    epd_deinit();

    initialized = false;
    TT_LOG_I(TAG, "EPDiy deinitialized");

    return true;
}

void EpdiyDisplay::setPowerOn(bool turnOn) {
    if (powered == turnOn) {
        return;
    }

    if (turnOn) {
        epd_poweron();
        powered = true;
        TT_LOG_D(TAG, "EPD power on");
    } else {
        epd_poweroff();
        powered = false;
        TT_LOG_D(TAG, "EPD power off");
    }
}

// LVGL functions
bool EpdiyDisplay::startLvgl() {
    if (lvglDisplay != nullptr) {
        TT_LOG_W(TAG, "LVGL already initialized");
        return true;
    }

    if (!initialized) {
        TT_LOG_E(TAG, "EPD not initialized, call start() first");
        return false;
    }

    // Get the native display dimensions
    uint16_t width = epd_width();
    uint16_t height = epd_height();

    TT_LOG_I(TAG, "Creating LVGL display: %dx%d (EPDiy rotation: %d)", width, height, configuration->rotation);

    // Create LVGL display with native dimensions
    lvglDisplay = lv_display_create(width, height);
    if (lvglDisplay == nullptr) {
        TT_LOG_E(TAG, "Failed to create LVGL display");
        return false;
    }

    // EPD uses 4-bit grayscale (16 levels)
    // Map to LVGL's L8 format (8-bit grayscale)
    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_L8);
    auto lv_rotation = epdRotationToLvgl(configuration->rotation);
    lv_display_set_rotation(lvglDisplay, lv_rotation);

    // Allocate draw buffer
    // EPDiy already allocates a buffer, so we only need one
    size_t draw_buffer_size = width * height;
    
    // Allocate in SPIRAM if available, otherwise internal RAM
    lvglDrawBuffer = static_cast<uint8_t*>(heap_caps_malloc(draw_buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (lvglDrawBuffer == nullptr) {
        lvglDrawBuffer = static_cast<uint8_t*>(heap_caps_malloc(draw_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    }
    
    if (lvglDrawBuffer == nullptr) {
        TT_LOG_E(TAG, "Failed to allocate draw buffer 1");
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
    TT_LOG_D(TAG, "Registered rotation change event callback");

    // Start touch device if present
    auto touch_device = getTouchDevice();
    if (touch_device != nullptr && touch_device->supportsLvgl()) {
        TT_LOG_D(TAG, "Starting touch device for LVGL");
        if (!touch_device->startLvgl(lvglDisplay)) {
            TT_LOG_W(TAG, "Failed to start touch device for LVGL");
        }
    }

    TT_LOG_I(TAG, "LVGL display initialized");
    return true;
}

bool EpdiyDisplay::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return false;
    }

    TT_LOG_I(TAG, "Stopping LVGL display");

    // Stop touch device
    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->stopLvgl();
    }

    // Free draw buffer
    if (lvglDrawBuffer != nullptr) {
        heap_caps_free(lvglDrawBuffer);
        lvglDrawBuffer = nullptr;
    }

    // Delete display (this will automatically free the buffers)
    lv_display_delete(lvglDisplay);
    lvglDisplay = nullptr;


    TT_LOG_I(TAG, "LVGL display stopped");
    return true;
}

void EpdiyDisplay::flushCallback(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap) {
    uint64_t t0 = esp_timer_get_time();
    auto* instance = static_cast<EpdiyDisplay*>(lv_display_get_user_data(display));
    if (instance != nullptr) {
        instance->flushInternal(area, pixelMap);
    }
    TT_LOG_I(TAG, "flush took %llu us", (unsigned long long)(esp_timer_get_time() - t0));
    lv_display_flush_ready(display);
}


// EPD functions
void EpdiyDisplay::clearScreen() {
    if (!initialized) {
        TT_LOG_E(TAG, "EPD not initialized");
        return;
    }

    std::lock_guard<tt::Mutex> guard(*lock);
    
    if (!powered) {
        setPowerOn(true);
    }
    
    epd_clear();
    
    // Also clear the framebuffer
    epd_hl_set_all_white(&highlevelState);
}

void EpdiyDisplay::clearArea(EpdRect area) {
    if (!initialized) {
        TT_LOG_E(TAG, "EPD not initialized");
        return;
    }

    std::lock_guard<tt::Mutex> guard(*lock);
    
    if (!powered) {
        setPowerOn(true);
    }
    
    epd_clear_area(area);
}

enum EpdDrawError EpdiyDisplay::updateScreen(enum EpdDrawMode mode, int temperature) {
    if (!initialized) {
        TT_LOG_E(TAG, "EPD not initialized");
        return EPD_DRAW_FAILED_ALLOC;
    }

    std::lock_guard<tt::Mutex> guard(*lock);
    
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
        TT_LOG_E(TAG, "EPD not initialized");
        return EPD_DRAW_FAILED_ALLOC;
    }

    std::lock_guard<tt::Mutex> guard(*lock);
    
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
        TT_LOG_E(TAG, "EPD not initialized");
        return;
    }

    std::lock_guard<tt::Mutex> guard(*lock);
    epd_hl_set_all_white(&highlevelState);
}

// Internal functions
void EpdiyDisplay::flushInternal(const lv_area_t* area, uint8_t* pixelMap) {
    if (!initialized) {
        TT_LOG_E(TAG, "Cannot flush - EPD not initialized");
        return;
    }

    // Lock for thread safety
    std::lock_guard<tt::Mutex> guard(*lock);

    // LVGL area coordinates (already in the rotated coordinate space that LVGL uses)
    int x = area->x1;
    int y = area->y1;
    int width = lv_area_get_width(area);
    int height = lv_area_get_height(area);
    
    TT_LOG_D(TAG, "Flushing area: x=%d, y=%d, w=%d, h=%d", x, y, width, height);

    // Convert 8-bit grayscale to 4-bit and pack 2 pixels/byte with row padding
    const int row_stride_bytes = (width + 1) / 2; // bytes per row
    const size_t packedSize = static_cast<size_t>(row_stride_bytes) * height;
    std::vector<uint8_t> pixelMap4bit(packedSize, 0); // Initialize with zeros
    
    // Pack pixels row by row to avoid skewing with odd widths
    for (int row = 0; row < height; ++row) {
        const uint8_t* src = pixelMap + static_cast<size_t>(row) * width;
        uint8_t* dst = pixelMap4bit.data() + static_cast<size_t>(row) * row_stride_bytes;
        for (int col = 0; col < width; col += 2) {
            const uint8_t p0 = src[col] / 17;
            const uint8_t p1 = (col + 1 < width) ? (src[col + 1] / 17) : 0;
            // Pack two 4-bit pixels into one byte: p1 in upper nibble, p0 in lower nibble
            dst[col / 2] = static_cast<uint8_t>((p1 << 4) | p0);
        }
    }

    // Update the screen area
    // Use the rotated coordinates for the update area as well
    EpdRect update_area = {
        .x = x,
        .y = y,
        .width = static_cast<uint16_t>(width),
        .height = static_cast<uint16_t>(height)
    };

    epd_draw_rotated_image(
        update_area, 
        pixelMap4bit.data(), 
        framebuffer
    );
    
    epd_hl_update_area(
        &highlevelState, 
        static_cast<EpdDrawMode>(configuration->defaultDrawMode | MODE_PACKING_2PPB), 
        configuration->defaultTemperature,
        update_area
    );
}

lv_display_rotation_t EpdiyDisplay::epdRotationToLvgl(enum EpdRotation epdRotation) {
    // Static lookup table for EPD -> LVGL rotation mapping
    // EPDiy: LANDSCAPE = 0°, PORTRAIT = 90° CW, INVERTED_LANDSCAPE = 180°, INVERTED_PORTRAIT = 270° CW
    // LVGL: 0 = 0°, 90 = 90° CW, 180 = 180°, 270 = 270° CW
    static const lv_display_rotation_t rotationMap[] = {
        LV_DISPLAY_ROTATION_0,    // EPD_ROT_LANDSCAPE (0)
        LV_DISPLAY_ROTATION_270,  // EPD_ROT_PORTRAIT (1) - 90° CW in EPD is 270° in LVGL
        LV_DISPLAY_ROTATION_180,  // EPD_ROT_INVERTED_LANDSCAPE (2)
        LV_DISPLAY_ROTATION_90    // EPD_ROT_INVERTED_PORTRAIT (3) - 270° CW in EPD is 90° in LVGL
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
        EPD_ROT_LANDSCAPE,           // LV_DISPLAY_ROTATION_0 (0)
        EPD_ROT_INVERTED_PORTRAIT,   // LV_DISPLAY_ROTATION_90 (1)
        EPD_ROT_INVERTED_LANDSCAPE,  // LV_DISPLAY_ROTATION_180 (2)
        EPD_ROT_PORTRAIT             // LV_DISPLAY_ROTATION_270 (3)
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
    TT_LOG_I(TAG, "LVGL rotation changed to %d, setting EPDiy rotation to %d", lvgl_rotation, epd_rotation);
    epd_set_rotation(epd_rotation);
    
    // Update configuration to keep it in sync
    configuration->rotation = epd_rotation;
    
    // Log the new dimensions
    TT_LOG_I(TAG, "Display dimensions after rotation: %dx%d", 
             epd_rotated_display_width(), epd_rotated_display_height());
}

