#include "Trackball.h"

#include <Tactility/Assets.h>
#include <Tactility/Logger.h>
#include <atomic>

static const auto LOGGER = tt::Logger("Trackball");

namespace trackball {

static TrackballConfig g_config;
static lv_indev_t* g_indev = nullptr;
static std::atomic<bool> g_initialized{false};
static std::atomic<bool> g_enabled{true};
static std::atomic<Mode> g_mode{Mode::Encoder};

// Interrupt-driven position tracking (atomic for ISR safety)
static std::atomic<int32_t> g_cursorX{160};
static std::atomic<int32_t> g_cursorY{120};
static std::atomic<bool> g_buttonPressed{false};

// Encoder mode: accumulated diff since last read
static std::atomic<int32_t> g_encoderDiff{0};

// Sensitivity cached for ISR access (atomic for thread safety)
static std::atomic<int32_t> g_encoderSensitivity{1};   // Steps per tick for encoder
static std::atomic<int32_t> g_pointerSensitivity{10};  // Pixels per tick for pointer

// Cursor object for pointer mode
static lv_obj_t* g_cursor = nullptr;

// Screen dimensions (T-Deck: 320x240)
static constexpr int32_t SCREEN_WIDTH = 320;
static constexpr int32_t SCREEN_HEIGHT = 240;

static constexpr int32_t CURSOR_SIZE = 16;

// ISR handler for trackball directions
static void IRAM_ATTR trackball_isr_handler(void* arg) {
    gpio_num_t pin = static_cast<gpio_num_t>(reinterpret_cast<intptr_t>(arg));

    if (g_mode.load(std::memory_order_relaxed) == Mode::Pointer) {
        // Pointer mode: update absolute position using atomic fetch_add/sub
        // Clamping is done in read_cb to avoid race conditions
        int32_t step = g_pointerSensitivity.load(std::memory_order_relaxed);
        if (pin == g_config.pinRight) {
            g_cursorX.fetch_add(step, std::memory_order_relaxed);
        } else if (pin == g_config.pinLeft) {
            g_cursorX.fetch_sub(step, std::memory_order_relaxed);
        } else if (pin == g_config.pinUp) {
            g_cursorY.fetch_sub(step, std::memory_order_relaxed);
        } else if (pin == g_config.pinDown) {
            g_cursorY.fetch_add(step, std::memory_order_relaxed);
        }
    } else {
        // Encoder mode: accumulate diff
        int32_t step = g_encoderSensitivity.load(std::memory_order_relaxed);
        if (pin == g_config.pinRight || pin == g_config.pinDown) {
            g_encoderDiff.fetch_add(step, std::memory_order_relaxed);
        } else if (pin == g_config.pinLeft || pin == g_config.pinUp) {
            g_encoderDiff.fetch_sub(step, std::memory_order_relaxed);
        }
    }
}

// ISR handler for button (any edge)
static void IRAM_ATTR button_isr_handler(void* arg) {
    // Read current button state (active low)
    bool pressed = gpio_get_level(g_config.pinClick) == 0;
    g_buttonPressed.store(pressed, std::memory_order_relaxed);
}

// Helper to clamp value to range
static inline int32_t clamp(int32_t val, int32_t minVal, int32_t maxVal) {
    if (val < minVal) return minVal;
    if (val > maxVal) return maxVal;
    return val;
}

static void read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    Mode currentMode = g_mode.load(std::memory_order_relaxed);

    if (!g_initialized.load(std::memory_order_relaxed) || !g_enabled.load(std::memory_order_relaxed)) {
        data->state = LV_INDEV_STATE_RELEASED;
        if (currentMode == Mode::Encoder) {
            data->enc_diff = 0;
        } else {
            // Clamp cursor position to screen bounds
            int32_t x = clamp(g_cursorX.load(std::memory_order_relaxed), 0, SCREEN_WIDTH - CURSOR_SIZE - 1);
            int32_t y = clamp(g_cursorY.load(std::memory_order_relaxed), 0, SCREEN_HEIGHT - CURSOR_SIZE - 1);
            g_cursorX.store(x, std::memory_order_relaxed);
            g_cursorY.store(y, std::memory_order_relaxed);
            data->point.x = static_cast<int16_t>(x);
            data->point.y = static_cast<int16_t>(y);
        }
        return;
    }

    if (currentMode == Mode::Encoder) {
        // Read and reset accumulated encoder diff
        int32_t diff = g_encoderDiff.exchange(0);
        data->enc_diff = static_cast<int16_t>(diff);

        if (diff != 0) {
            lv_disp_trig_activity(nullptr);
        }
    } else {
        // Pointer mode: read and clamp cursor position
        int32_t x = clamp(g_cursorX.load(std::memory_order_relaxed), 0, SCREEN_WIDTH - CURSOR_SIZE - 1);
        int32_t y = clamp(g_cursorY.load(std::memory_order_relaxed), 0, SCREEN_HEIGHT - CURSOR_SIZE - 1);

        // Store clamped values back to prevent unbounded growth
        g_cursorX.store(x, std::memory_order_relaxed);
        g_cursorY.store(y, std::memory_order_relaxed);

        data->point.x = static_cast<int16_t>(x);
        data->point.y = static_cast<int16_t>(y);
    }

    // Button state (same for both modes)
    bool pressed = g_buttonPressed.load(std::memory_order_relaxed);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;

    if (pressed) {
        lv_disp_trig_activity(nullptr);
    }
}

lv_indev_t* init(const TrackballConfig& config) {
    if (g_initialized.load(std::memory_order_relaxed)) {
        LOGGER.warn("Already initialized");
        return g_indev;
    }

    g_config = config;

    // Set default sensitivities if not specified
    if (g_config.encoderSensitivity == 0) {
        g_config.encoderSensitivity = 1;
    }
    if (g_config.pointerSensitivity == 0) {
        g_config.pointerSensitivity = 10;
    }
    g_encoderSensitivity.store(g_config.encoderSensitivity, std::memory_order_relaxed);
    g_pointerSensitivity.store(g_config.pointerSensitivity, std::memory_order_relaxed);

    // Initialize cursor position to center
    g_cursorX.store(SCREEN_WIDTH / 2, std::memory_order_relaxed);
    g_cursorY.store(SCREEN_HEIGHT / 2, std::memory_order_relaxed);
    g_encoderDiff.store(0, std::memory_order_relaxed);
    g_buttonPressed.store(false, std::memory_order_relaxed);

    // Configure direction pins as interrupt inputs (falling edge)
    const gpio_num_t dirPins[4] = {
        config.pinRight,
        config.pinUp,
        config.pinLeft,
        config.pinDown
    };

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;  // Falling edge (active low)
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;

    // Install GPIO ISR service (if not already installed)
    static bool isr_service_installed = false;
    if (!isr_service_installed) {
        esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            // ESP_ERR_INVALID_STATE means already installed, which is fine
            isr_service_installed = true;
        } else {
            LOGGER.error("Failed to install GPIO ISR service: {}", esp_err_to_name(err));
            return nullptr;
        }
    }

    // Track added handlers for cleanup on failure
    int handlersAdded = 0;

    // Configure and attach ISR for direction pins
    for (int i = 0; i < 4; i++) {
        io_conf.pin_bit_mask = (1ULL << dirPins[i]);
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            LOGGER.error("Failed to configure GPIO {}: {}", static_cast<int>(dirPins[i]), esp_err_to_name(err));
            // Cleanup previously added handlers
            for (int j = 0; j < handlersAdded; j++) {
                gpio_isr_handler_remove(dirPins[j]);
            }
            return nullptr;
        }

        err = gpio_isr_handler_add(dirPins[i], trackball_isr_handler, reinterpret_cast<void*>(static_cast<intptr_t>(dirPins[i])));
        if (err != ESP_OK) {
            LOGGER.error("Failed to add ISR for GPIO {}: {}", static_cast<int>(dirPins[i]), esp_err_to_name(err));
            // Cleanup previously added handlers
            for (int j = 0; j < handlersAdded; j++) {
                gpio_isr_handler_remove(dirPins[j]);
            }
            return nullptr;
        }
        handlersAdded++;
    }

    // Configure button pin (any edge for press/release detection)
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.pin_bit_mask = (1ULL << config.pinClick);
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        LOGGER.error("Failed to configure button GPIO {}: {}", static_cast<int>(config.pinClick), esp_err_to_name(err));
        // Cleanup direction handlers
        for (int i = 0; i < 4; i++) {
            gpio_isr_handler_remove(dirPins[i]);
        }
        return nullptr;
    }

    err = gpio_isr_handler_add(config.pinClick, button_isr_handler, nullptr);
    if (err != ESP_OK) {
        LOGGER.error("Failed to add button ISR: {}", esp_err_to_name(err));
        // Cleanup direction handlers
        for (int i = 0; i < 4; i++) {
            gpio_isr_handler_remove(dirPins[i]);
        }
        return nullptr;
    }

    // Read initial button state
    g_buttonPressed.store(gpio_get_level(config.pinClick) == 0);

    // Register as LVGL encoder input device for group navigation (default mode)
    g_indev = lv_indev_create();
    lv_indev_set_type(g_indev, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(g_indev, read_cb);

    if (g_indev != nullptr) {
        g_initialized.store(true, std::memory_order_relaxed);
        LOGGER.info("Initialized with interrupts (R:{} U:{} L:{} D:{} Click:{})",
                 static_cast<int>(config.pinRight),
                 static_cast<int>(config.pinUp),
                 static_cast<int>(config.pinLeft),
                 static_cast<int>(config.pinDown),
                 static_cast<int>(config.pinClick));
    } else {
        LOGGER.error("Failed to register LVGL input device");
    }

    return g_indev;
}

// Create cursor for pointer mode
static void createCursor() {
    if (g_cursor != nullptr || g_indev == nullptr) return;

    g_cursor = lv_image_create(lv_layer_sys());
    if (g_cursor != nullptr) {
        lv_obj_remove_flag(g_cursor, LV_OBJ_FLAG_CLICKABLE);

        // Set cursor image
        lv_image_set_src(g_cursor, TT_ASSETS_UI_CURSOR);
        lv_indev_set_cursor(g_indev, g_cursor);
        LOGGER.debug("Cursor created");
    }
}

// Destroy cursor when switching back to encoder mode
static void destroyCursor() {
    if (g_cursor == nullptr) return;

    // Delete the cursor object - this automatically detaches it from the indev
    lv_obj_delete(g_cursor);
    g_cursor = nullptr;
    LOGGER.debug("Cursor destroyed");
}

void deinit() {
    if (!g_initialized.load(std::memory_order_relaxed)) return;

    destroyCursor();

    // Disable interrupts and remove ISR handlers
    const gpio_num_t pins[5] = {
        g_config.pinRight,
        g_config.pinUp,
        g_config.pinLeft,
        g_config.pinDown,
        g_config.pinClick
    };

    for (int i = 0; i < 5; i++) {
        gpio_intr_disable(pins[i]);
        gpio_isr_handler_remove(pins[i]);
    }

    if (g_indev) {
        lv_indev_delete(g_indev);
        g_indev = nullptr;
    }

    g_initialized.store(false, std::memory_order_relaxed);
    g_mode.store(Mode::Encoder, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_relaxed);
    LOGGER.info("Deinitialized");
}

void setEncoderSensitivity(uint8_t sensitivity) {
    if (sensitivity > 0) {
        // Only update the atomic - ISR reads from atomic, not g_config
        g_encoderSensitivity.store(sensitivity, std::memory_order_relaxed);
        LOGGER.debug("Encoder sensitivity set to {}", sensitivity);
    }
}

void setPointerSensitivity(uint8_t sensitivity) {
    if (sensitivity > 0) {
        // Only update the atomic - ISR reads from atomic, not g_config
        g_pointerSensitivity.store(sensitivity, std::memory_order_relaxed);
        LOGGER.debug("Pointer sensitivity set to {}", sensitivity);
    }
}

void setEnabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);

    // Hide/show cursor based on enabled state when in pointer mode
    // Note: Must be called from LVGL thread (main thread) for thread safety
    lv_obj_t* cursor = g_cursor;  // Local copy to avoid race with setMode
    if (cursor != nullptr) {
        if (enabled) {
            lv_obj_clear_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
        }
    }

    LOGGER.info("{}", enabled ? "Enabled" : "Disabled");
}

void setMode(Mode mode) {
    // Note: Must be called from LVGL thread (main thread) for thread safety
    if (!g_initialized.load(std::memory_order_relaxed) || g_indev == nullptr) {
        LOGGER.warn("Cannot set mode - not initialized");
        return;
    }

    if (g_mode.load(std::memory_order_relaxed) == mode) {
        return;
    }

    g_mode.store(mode, std::memory_order_relaxed);

    if (mode == Mode::Pointer) {
        // Switch to pointer mode
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_POINTER);
        createCursor();
        // Reset cursor to center when switching modes
        g_cursorX.store(SCREEN_WIDTH / 2, std::memory_order_relaxed);
        g_cursorY.store(SCREEN_HEIGHT / 2, std::memory_order_relaxed);
        LOGGER.info("Switched to Pointer mode");
    } else {
        // Switch to encoder mode
        destroyCursor();
        lv_indev_set_type(g_indev, LV_INDEV_TYPE_ENCODER);
        g_encoderDiff.store(0, std::memory_order_relaxed);  // Reset encoder diff
        LOGGER.info("Switched to Encoder mode");
    }
}

Mode getMode() {
    return g_mode.load(std::memory_order_relaxed);
}

}
