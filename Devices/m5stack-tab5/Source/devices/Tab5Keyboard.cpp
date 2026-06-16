#include "Tab5Keyboard.h"
#include <Tactility/app/App.h>
#include <Tactility/lvgl/Keyboard.h>
#include <Tactility/lvgl/LvglSync.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/log.h>
#include <esp_timer.h>
#include <lvgl.h>

// ---------------------------------------------------------------------------
// Register addresses
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_INT_CFG        = 0x00;
static constexpr uint8_t REG_INT_STAT       = 0x01;
static constexpr uint8_t REG_EVENT_NUM      = 0x02;
static constexpr uint8_t REG_BRIGHTNESS     = 0x03;
static constexpr uint8_t REG_KEYBOARD_MODE  = 0x10;
static constexpr uint8_t REG_RGB_MODE       = 0x11;
static constexpr uint8_t REG_KEY_EVENT      = 0x20;
static constexpr uint8_t REG_RGB_BASE       = 0x60;
static constexpr uint8_t KEY_EVENT_EMPTY    = 0xFF;

// ---------------------------------------------------------------------------
// Modifier key positions in the 5x14 matrix
// ---------------------------------------------------------------------------
static constexpr uint8_t MOD_ROW_SYM  = 3, MOD_COL_SYM  = 0;
static constexpr uint8_t MOD_ROW_AA   = 3, MOD_COL_AA   = 1;
static constexpr uint8_t MOD_ROW_CTRL = 4, MOD_COL_CTRL = 0;
static constexpr uint8_t MOD_ROW_ALT  = 4, MOD_COL_ALT  = 1;

// ---------------------------------------------------------------------------
// HID lookup tables
// Row-major: index = row * 14 + col, 5 rows x 14 cols = 70 entries.
// modifier 0x02 = Left Shift (pre-baked by firmware for shifted characters).
// ---------------------------------------------------------------------------
struct HidMapping {
    uint8_t keycode;
    uint8_t modifier;
};

static constexpr HidMapping KEY_MATRIX_HID_BASE[70] = {
    // Row 0: Esc 1 2 3 4 5 6 7 8 9 0 - + Del
    {0x29, 0x00}, {0x1E, 0x00}, {0x1F, 0x00}, {0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00},
    {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x2D, 0x00},
    {0x2E, 0x02}, {0x4C, 0x00},
    // Row 1: ` ! @ # $ % ^ & * ( ) [ ] backslash
    {0x35, 0x00}, {0x1E, 0x02}, {0x1F, 0x02}, {0x20, 0x02}, {0x21, 0x02}, {0x22, 0x02},
    {0x23, 0x02}, {0x24, 0x02}, {0x25, 0x02}, {0x26, 0x02}, {0x27, 0x02}, {0x2F, 0x00},
    {0x30, 0x00}, {0x31, 0x00},
    // Row 2: Tab q w e r t y u i o p ; ' Backspace
    {0x2B, 0x00}, {0x14, 0x00}, {0x1A, 0x00}, {0x08, 0x00}, {0x15, 0x00}, {0x17, 0x00},
    {0x1C, 0x00}, {0x18, 0x00}, {0x0C, 0x00}, {0x12, 0x00}, {0x13, 0x00}, {0x33, 0x00},
    {0x34, 0x00}, {0x2A, 0x00},
    // Row 3: Sym Aa a s d f g h j k l ↑ _ Enter
    {0x00, 0x00}, {0x00, 0x00}, {0x04, 0x00}, {0x16, 0x00}, {0x07, 0x00}, {0x09, 0x00},
    {0x0A, 0x00}, {0x0B, 0x00}, {0x0D, 0x00}, {0x0E, 0x00}, {0x0F, 0x00}, {0x52, 0x00},
    {0x2D, 0x02}, {0x28, 0x00},
    // Row 4: Ctrl Alt z x c v b n m . ← ↓ → Space
    {0x00, 0x00}, {0x00, 0x00}, {0x1D, 0x00}, {0x1B, 0x00}, {0x06, 0x00}, {0x19, 0x00},
    {0x05, 0x00}, {0x11, 0x00}, {0x10, 0x00}, {0x37, 0x00}, {0x50, 0x00}, {0x51, 0x00},
    {0x4F, 0x00}, {0x2C, 0x00},
};

static constexpr HidMapping KEY_MATRIX_HID_SYM[70] = {
    // Row 0: identical to base
    {0x29, 0x00}, {0x1E, 0x00}, {0x1F, 0x00}, {0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00},
    {0x23, 0x00}, {0x24, 0x00}, {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x2D, 0x00},
    {0x2E, 0x02}, {0x4C, 0x00},
    // Row 1: Sym deltas: ` → ~, ! → ?, * → /, ( → <, ) → >, [ → {, ] → }, backslash → |
    {0x35, 0x02}, {0x38, 0x02}, {0x1F, 0x02}, {0x20, 0x02}, {0x21, 0x02}, {0x22, 0x02},
    {0x23, 0x02}, {0x24, 0x02}, {0x38, 0x00}, {0x36, 0x02}, {0x37, 0x02}, {0x2F, 0x02},
    {0x30, 0x02}, {0x31, 0x02},
    // Row 2: Sym deltas: ; → :, ' → "
    {0x2B, 0x00}, {0x14, 0x00}, {0x1A, 0x00}, {0x08, 0x00}, {0x15, 0x00}, {0x17, 0x00},
    {0x1C, 0x00}, {0x18, 0x00}, {0x0C, 0x00}, {0x12, 0x00}, {0x13, 0x00}, {0x33, 0x02},
    {0x34, 0x02}, {0x2A, 0x00},
    // Row 3: Sym delta: _ → =
    {0x00, 0x00}, {0x00, 0x00}, {0x04, 0x00}, {0x16, 0x00}, {0x07, 0x00}, {0x09, 0x00},
    {0x0A, 0x00}, {0x0B, 0x00}, {0x0D, 0x00}, {0x0E, 0x00}, {0x0F, 0x00}, {0x52, 0x00},
    {0x2E, 0x00}, {0x28, 0x00},
    // Row 4: Sym delta: . → ,
    {0x00, 0x00}, {0x00, 0x00}, {0x1D, 0x00}, {0x1B, 0x00}, {0x06, 0x00}, {0x19, 0x00},
    {0x05, 0x00}, {0x11, 0x00}, {0x10, 0x00}, {0x36, 0x00}, {0x50, 0x00}, {0x51, 0x00},
    {0x4F, 0x00}, {0x2C, 0x00},
};

// ---------------------------------------------------------------------------
// HID usage code + modifier → LVGL key
// Covers all codes present in the Tab5 matrix tables above.
// ---------------------------------------------------------------------------
static uint32_t tab5TranslateKey(uint8_t keycode, uint8_t modifier, bool ctrl) {
    const bool shift = (modifier & 0x22U) != 0U;

    // Navigation → LVGL key constants
    switch (keycode) {
        case 0x29: return LV_KEY_ESC;
        case 0x28: return LV_KEY_ENTER;
        case 0x2A: return LV_KEY_BACKSPACE;
        case 0x4C: return LV_KEY_DEL;
        case 0x2B: return '\t';
        // Arrows: Ctrl+arrow = focus navigation, plain arrow = raw cursor movement
        case 0x52: return ctrl ? (uint32_t)LV_KEY_PREV  : (uint32_t)LV_KEY_UP;
        case 0x51: return ctrl ? (uint32_t)LV_KEY_NEXT  : (uint32_t)LV_KEY_DOWN;
        case 0x50: return ctrl ? (uint32_t)LV_KEY_PREV  : (uint32_t)LV_KEY_LEFT;
        case 0x4F: return ctrl ? (uint32_t)LV_KEY_NEXT  : (uint32_t)LV_KEY_RIGHT;
        default:   break;
    }

    // Letters a–z / A–Z
    if (keycode >= 0x04U && keycode <= 0x1DU) {
        uint32_t c = static_cast<uint32_t>('a' + (keycode - 0x04U));
        return shift ? (c - 0x20U) : c;
    }

    // Numbers 1–0 and their shifted symbols
    if (keycode >= 0x1EU && keycode <= 0x27U) {
        static constexpr char nums[]  = "1234567890";
        static constexpr char snums[] = "!@#$%^&*()";
        return shift ? static_cast<uint32_t>(snums[keycode - 0x1EU])
                     : static_cast<uint32_t>(nums[keycode - 0x1EU]);
    }

    // Space and punctuation - all codes present in the Tab5 matrix
    switch (keycode) {
        case 0x2C: return ' ';
        case 0x2D: return shift ? '_'  : '-';
        case 0x2E: return shift ? '+'  : '=';
        case 0x2F: return shift ? '{'  : '[';
        case 0x30: return shift ? '}'  : ']';
        case 0x31: return shift ? '|'  : '\\';
        case 0x33: return shift ? ':'  : ';';
        case 0x34: return shift ? '"'  : '\'';
        case 0x35: return shift ? '~'  : '`';
        case 0x36: return shift ? '<'  : ',';
        case 0x37: return shift ? '>'  : '.';
        case 0x38: return shift ? '?'  : '/';
        default:   return 0;
    }
}

// ---------------------------------------------------------------------------
// I2C helpers - use Tactility I2C controller API
// ---------------------------------------------------------------------------
bool Tab5Keyboard::readReg(uint8_t reg, uint8_t& value) {
    return i2c_controller_read_register(i2cController, I2C_ADDRESS, reg, &value, 1, pdMS_TO_TICKS(50)) == ERROR_NONE;
}

bool Tab5Keyboard::writeReg(uint8_t reg, uint8_t value) {
    return i2c_controller_write_register(i2cController, I2C_ADDRESS, reg, &value, 1, pdMS_TO_TICKS(50)) == ERROR_NONE;
}

// ---------------------------------------------------------------------------
// LED helpers - LED0 = Sym indicator (green), LED1 = Aa indicator (red)
// RGB register layout: [B, G, R] per LED, stride 4 (byte 3 reserved)
// ---------------------------------------------------------------------------
void Tab5Keyboard::updateLeds() {
    // [LED0: B,G,R, reserved, LED1: B,G,R]
    uint8_t buf[7] = {
        0x00, symActive  ? uint8_t(0xA0) : uint8_t(0x00), 0x00, 0x00,  // LED0: green if Sym
        0x00, 0x00, aaSticky ? uint8_t(0xA0) : uint8_t(0x00),          // LED1: red if Aa latched
    };
    // Write 7-byte block starting at REG_RGB_BASE
    i2c_controller_write_register(i2cController, I2C_ADDRESS, REG_RGB_BASE, buf, 7, pdMS_TO_TICKS(50));
}

// ---------------------------------------------------------------------------
// IRQ pin - GPIO 50, active-low, falling edge
// ---------------------------------------------------------------------------
void IRAM_ATTR Tab5Keyboard::irqHandler(void* arg) {
    auto* self = static_cast<Tab5Keyboard*>(arg);
    self->irqPending = true;
}

bool Tab5Keyboard::configureIrqPin() {
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask  = (1ULL << INT_PIN);
    io_conf.mode          = GPIO_MODE_INPUT;
    io_conf.pull_up_en    = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type     = GPIO_INTR_NEGEDGE;
    if (gpio_config(&io_conf) != ESP_OK) {
        return false;
    }

    const esp_err_t svc = gpio_install_isr_service(0);
    if (svc != ESP_OK && svc != ESP_ERR_INVALID_STATE) {
        return false;
    }

    if (gpio_isr_handler_add(INT_PIN, irqHandler, this) != ESP_OK) {
        gpio_set_intr_type(INT_PIN, GPIO_INTR_DISABLE);
        return false;
    }

    irqConfigured = true;
    return true;
}

void Tab5Keyboard::removeIrqPin() {
    if (!irqConfigured) return;
    gpio_isr_handler_remove(INT_PIN);
    irqConfigured = false;
    irqPending    = false;
}

// ---------------------------------------------------------------------------
// drainEvents - reads all pending events from the device queue
// ---------------------------------------------------------------------------
void Tab5Keyboard::drainEvents() {
    uint8_t count = 0;
    if (!readReg(REG_EVENT_NUM, count) || count == 0) {
        return;
    }

    while (count > 0) {
        uint8_t raw = 0;
        if (!readReg(REG_KEY_EVENT, raw) || raw == KEY_EVENT_EMPTY) {
            break;
        }

        const bool    pressed = (raw & 0x80U) != 0U;
        const uint8_t row     = (raw >> 4U) & 0x07U;
        const uint8_t col     = raw & 0x0FU;

        // Modifier keys: update state, no key output
        if (row == MOD_ROW_SYM && col == MOD_COL_SYM) {
            symActive = pressed;
            updateLeds();
            count--;
            continue;
        }
        if (row == MOD_ROW_AA && col == MOD_COL_AA) {
            if (pressed) {
                aaHeld   = true;
                aaTapped = true;  // assume tap until a real key is pressed while held
            } else {
                // Only latch sticky if no non-modifier key was pressed during this hold
                if (aaTapped) {
                    aaSticky = !aaSticky;
                }
                aaHeld   = false;
                aaTapped = false;
            }
            updateLeds();
            count--;
            continue;
        }
        if (row == MOD_ROW_CTRL && col == MOD_COL_CTRL) {
            ctrlHeld = pressed;
            count--;
            continue;
        }
        if (row == MOD_ROW_ALT && col == MOD_COL_ALT) {
            count--;
            continue;
        }

        if (row < 5U && col < 14U) {
            const bool aaActive = aaHeld || aaSticky;
            const HidMapping& m = symActive
                ? KEY_MATRIX_HID_SYM[row * 14U + col]
                : KEY_MATRIX_HID_BASE[row * 14U + col];
            if (m.keycode != 0U) {
                const uint8_t  modifier = static_cast<uint8_t>(m.modifier | (aaActive ? 0x02U : 0U));
                const uint32_t lv_key   = tab5TranslateKey(m.keycode, modifier, ctrlHeld);
                if (lv_key != 0U) {
                    if (pressed) {
                        // A real key was pressed — this hold is a chord, not a tap
                        aaTapped = false;
                        if (lv_key == LV_KEY_ESC) {
                            tt::app::stop();
                        } else {
                            xQueueSend(queue, &lv_key, 0);
                            // Arm software repeat tracking by row/col to survive modifier changes
                            const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
                            repeatKey     = lv_key;
                            repeatRow     = row;
                            repeatCol     = col;
                            repeatStartMs = now_ms;
                            repeatLastMs  = 0;
                            // Consume sticky Aa after one keypress
                            if (aaSticky) {
                                aaSticky = false;
                                aaHeld   = false;
                                updateLeds();
                            }
                        }
                    } else if (row == repeatRow && col == repeatCol) {
                        // Match release by position, not translated value — survives sticky Aa clear
                        repeatKey = 0;
                    }
                }
            }
        }
        count--;
    }

    // Clear INT status after draining so the line de-asserts
    writeReg(REG_INT_STAT, 0x00);
}

// ---------------------------------------------------------------------------
// processKeyboard - called from 20ms Timer; IRQ-gated when INT is wired
// ---------------------------------------------------------------------------
void Tab5Keyboard::processKeyboard() {
    bool shouldDrain = false;
    if (irqConfigured) {
        if (irqPending) {
            irqPending = false;
            shouldDrain = true;
        }
    } else {
        // Polling: check INT_STA first — bit 0 = Normal mode event pending
        uint8_t status = 0;
        if (readReg(REG_INT_STAT, status) && (status & 0x01U)) {
            shouldDrain = true;
        }
    }
    if (shouldDrain) {
        drainEvents();
    }

    // Software key-repeat (runs every tick regardless of IRQ)
    if (repeatKey != 0U) {
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
        if ((now_ms - repeatStartMs) >= REPEAT_INITIAL_MS) {
            const uint32_t last = repeatLastMs;
            if (last == 0 || (now_ms - last) >= REPEAT_RATE_MS) {
                repeatLastMs = now_ms;
                xQueueSend(queue, &repeatKey, 0);
            }
        }
    }

    checkAttachState();
}

// ---------------------------------------------------------------------------
// applyAutoRotation - on attach, switches to landscape if not already (saving
// the prior rotation); on detach, restores the saved rotation if we were the
// ones who changed it. Only affects the live LVGL rotation, never persisted
// display settings.
// ---------------------------------------------------------------------------
void Tab5Keyboard::applyAutoRotation(bool keyboardAttached) {
    auto* display = lv_indev_get_display(kbHandle);
    if (display == nullptr) {
        return;
    }

    if (!tt::lvgl::lock(pdMS_TO_TICKS(100))) {
        return; // try again on the next attach-state check
    }

    if (keyboardAttached) {
        if (lv_display_get_rotation(display) != LV_DISPLAY_ROTATION_90) {
            savedRotation = lv_display_get_rotation(display);
            rotationOverrideActive = true;
            lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
        }
    } else {
        // Only restore if rotation is still what we set it to - if the user manually
        // changed it since attaching, respect their choice instead.
        if (rotationOverrideActive && lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_90) {
            lv_display_set_rotation(display, savedRotation);
        }
        rotationOverrideActive = false;
    }

    tt::lvgl::unlock();
}

// ---------------------------------------------------------------------------
// checkAttachState - throttled (~1s) hot-plug detection. Reapplies device
// register configuration and auto-rotation on detach/attach transitions.
// ---------------------------------------------------------------------------
void Tab5Keyboard::checkAttachState() {
    static constexpr uint32_t ATTACH_CHECK_TICKS = 50; // ~1s at 20ms/tick

    if (++attachCheckTickCounter < ATTACH_CHECK_TICKS) {
        return;
    }
    attachCheckTickCounter = 0;

    const bool attached = isAttached();
    if (attached == wasAttached) {
        return;
    }
    wasAttached = attached;

    if (attached) {
        reinitDevice();
    }
    applyAutoRotation(attached);
}

// ---------------------------------------------------------------------------
// lateStart - see header comment. Brings up LVGL input handling for a keyboard
// that wasn't attached at boot (startLvgl() wasn't called from attachDevices()).
// ---------------------------------------------------------------------------
bool Tab5Keyboard::lateStart() {
    if (kbHandle != nullptr) {
        return true; // already started
    }

    auto* display = lv_display_get_default();
    if (display == nullptr) {
        return false; // LVGL not ready yet
    }

    if (!tt::lvgl::lock(pdMS_TO_TICKS(100))) {
        return false; // try again on the next attach-state check
    }

    bool started = startLvgl(display);
    if (started) {
        tt::lvgl::hardware_keyboard_set_indev(kbHandle);

        // redraw() assigns every indev that exists at the time to the active screen's
        // input group. This indev didn't exist yet at the last redraw(), so it has no
        // group and won't deliver key events until the next app switch. Join the
        // current default group now so input works immediately on the visible screen.
        lv_indev_set_group(kbHandle, lv_group_get_default());
    }

    tt::lvgl::unlock();

    return started;
}

// ---------------------------------------------------------------------------
// LVGL read callback - called from the LVGL task
// ---------------------------------------------------------------------------
void Tab5Keyboard::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<Tab5Keyboard*>(lv_indev_get_user_data(indev));
    uint32_t lv_key = 0;
    if (xQueueReceive(self->queue, &lv_key, 0) == pdTRUE) {
        data->key   = lv_key;
        data->state = LV_INDEV_STATE_PRESSED;
        data->continue_reading = (uxQueueMessagesWaiting(self->queue) > 0);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---------------------------------------------------------------------------
// KeyboardDevice interface
// ---------------------------------------------------------------------------
Tab5Keyboard::~Tab5Keyboard() {
    if (inputTimer) {
        stopLvgl();  // tears down LVGL indev, IRQ, I2C bus
    }
    if (queue) {
        vQueueDelete(queue);
        queue = nullptr;
    }
}

// ---------------------------------------------------------------------------
// reinitDevice - (re)applies the device register configuration. Used at
// startLvgl() and again on hot-plug reattach, since the device's RGB mode and
// interrupt configuration are volatile and reset to power-on defaults when
// the keyboard is unplugged and reconnected.
// ---------------------------------------------------------------------------
void Tab5Keyboard::reinitDevice() {
    writeReg(REG_KEYBOARD_MODE, 0x00); // Normal mode
    writeReg(REG_EVENT_NUM, 0x00);     // flush event queue
    writeReg(REG_INT_STAT, 0x00);      // clear pending INT
    writeReg(REG_RGB_MODE, 0x01);      // Custom RGB mode (manual LED control)
    writeReg(REG_BRIGHTNESS, 50);      // 50% brightness
    updateLeds();                      // restore current LED state

    if (irqConfigured) {
        writeReg(REG_INT_CFG, 0x01);   // re-enable Normal-mode interrupt (bit 0)
    }
}

bool Tab5Keyboard::startLvgl(lv_display_t* display) {
    if (!queue) {
        LOG_E("Tab5Keyboard", "Input queue allocation failed — cannot start");
        return false;
    }

    symActive    = false;
    aaSticky     = false;
    aaHeld       = false;
    aaTapped     = false;
    ctrlHeld     = false;
    repeatKey    = 0;
    repeatRow    = 0xFF;
    repeatCol    = 0xFF;
    repeatLastMs = 0;

    configureIrqPin();  // best-effort; falls back to polling if it fails. Must run before
                         // reinitDevice() so REG_INT_CFG is written if IRQ setup succeeded.

    // Best-effort: if the keyboard isn't attached yet (e.g. started speculatively at
    // boot so it can be detected later via hot-plug), these I2C writes fail silently
    // and reinitDevice() runs again once attach is detected.
    reinitDevice();

    kbHandle = lv_indev_create();
    lv_indev_set_type(kbHandle, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(kbHandle, readCallback);
    lv_indev_set_display(kbHandle, display);
    lv_indev_set_user_data(kbHandle, this);

    wasAttached = isAttached();
    rotationOverrideActive = false;

    assert(inputTimer == nullptr);
    inputTimer = std::make_unique<tt::Timer>(tt::Timer::Type::Periodic, pdMS_TO_TICKS(20), [this] {
        processKeyboard();
    });
    inputTimer->start();

    if (wasAttached) {
        applyAutoRotation(true);
    }

    return true;
}

bool Tab5Keyboard::stopLvgl() {
    if (!inputTimer) {
        return false;  // Not started
    }
    inputTimer->stop();
    inputTimer = nullptr;

    removeIrqPin();
    if (queue) {
        xQueueReset(queue);  // discard unread keycodes so a restart begins with an empty buffer
    }
    writeReg(REG_INT_CFG, 0x00);  // disable all interrupts
    symActive = false;
    aaSticky  = false;
    aaHeld    = false;
    updateLeds();  // turn LEDs off

    lv_indev_delete(kbHandle);
    kbHandle = nullptr;

    return true;
}

bool Tab5Keyboard::isAttached() const {
    return i2c_controller_has_device_at_address(i2cController, I2C_ADDRESS, pdMS_TO_TICKS(100)) == ERROR_NONE;
}
