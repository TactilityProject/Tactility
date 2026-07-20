// SPDX-License-Identifier: Apache-2.0
#include "tab5_keyboard.h"

#include "devices_common.h"

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>
#include <tactility/lvgl_module.h>
#include <tactility/module.h>

#include <driver/gpio.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <lvgl.h>

#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "Tab5Keyboard";
#define GET_CONFIG(device) (static_cast<const Tab5KeyboardConfig*>((device)->config))

static constexpr uint8_t I2C_ADDRESS = 0x6D;

// Software key-repeat timing
static constexpr uint32_t REPEAT_INITIAL_MS = 400;
static constexpr uint32_t REPEAT_RATE_MS = 80;

// I2C event-poll interval - mirrors the old deprecated-HAL's 20ms Timer period. Drives both
// REG_INT_STAT polling (when no IRQ pin) and software key-repeat ticking.
static constexpr uint32_t POLL_INTERVAL_MS = 20;

// Hot-plug attach-state check interval. I2C probes can false-positive on a floating/half-connected
// bus (e.g. mid-unplug), so a state change is only acted on once it's seen on two consecutive
// checks in a row (see check_attach_state()).
static constexpr uint32_t ATTACH_CHECK_INTERVAL_MS = 1000;

// ---------------------------------------------------------------------------
// Register addresses
// ---------------------------------------------------------------------------
static constexpr uint8_t REG_INT_CFG = 0x00;
static constexpr uint8_t REG_INT_STAT = 0x01;
static constexpr uint8_t REG_EVENT_NUM = 0x02;
static constexpr uint8_t REG_BRIGHTNESS = 0x03;
static constexpr uint8_t REG_KEYBOARD_MODE = 0x10;
static constexpr uint8_t REG_RGB_MODE = 0x11;
static constexpr uint8_t REG_KEY_EVENT = 0x20;
static constexpr uint8_t REG_RGB_BASE = 0x60;
static constexpr uint8_t KEY_EVENT_EMPTY = 0xFF;

// ---------------------------------------------------------------------------
// Modifier key positions in the 5x14 matrix
// ---------------------------------------------------------------------------
static constexpr uint8_t MOD_ROW_SYM = 3, MOD_COL_SYM = 0;
static constexpr uint8_t MOD_ROW_AA = 3, MOD_COL_AA = 1;
static constexpr uint8_t MOD_ROW_CTRL = 4, MOD_COL_CTRL = 0;
static constexpr uint8_t MOD_ROW_ALT = 4, MOD_COL_ALT = 1;

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
// Covers all codes present in the Tab5 matrix tables above. LV_KEY_* are plain uint32_t
// constants - matching KeyboardKeyData::key's driver-defined contract and the same convention
// m5stack-module's cardputer_keyboard.cpp kernel driver already uses.
// ---------------------------------------------------------------------------
static uint32_t tab5_translate_key(uint8_t keycode, uint8_t modifier, bool ctrl) {
    const bool shift = (modifier & 0x22U) != 0U;

    // Navigation → LVGL key constants
    switch (keycode) {
        case 0x29: return LV_KEY_ESC;
        case 0x28: return LV_KEY_ENTER;
        case 0x2A: return LV_KEY_BACKSPACE;
        case 0x4C: return LV_KEY_DEL;
        case 0x2B: return '\t';
        // Arrows: Ctrl+arrow = focus navigation, plain arrow = raw cursor movement
        case 0x52: return ctrl ? (uint32_t)LV_KEY_PREV : (uint32_t)LV_KEY_UP;
        case 0x51: return ctrl ? (uint32_t)LV_KEY_NEXT : (uint32_t)LV_KEY_DOWN;
        case 0x50: return ctrl ? (uint32_t)LV_KEY_PREV : (uint32_t)LV_KEY_LEFT;
        case 0x4F: return ctrl ? (uint32_t)LV_KEY_NEXT : (uint32_t)LV_KEY_RIGHT;
        default: break;
    }

    // Letters a–z / A–Z
    if (keycode >= 0x04U && keycode <= 0x1DU) {
        uint32_t c = static_cast<uint32_t>('a' + (keycode - 0x04U));
        return shift ? (c - 0x20U) : c;
    }

    // Numbers 1–0 and their shifted symbols
    if (keycode >= 0x1EU && keycode <= 0x27U) {
        static constexpr char nums[] = "1234567890";
        static constexpr char snums[] = "!@#$%^&*()";
        return shift ? static_cast<uint32_t>(snums[keycode - 0x1EU])
                     : static_cast<uint32_t>(nums[keycode - 0x1EU]);
    }

    // Space and punctuation - all codes present in the Tab5 matrix
    switch (keycode) {
        case 0x2C: return ' ';
        case 0x2D: return shift ? '_' : '-';
        case 0x2E: return shift ? '+' : '=';
        case 0x2F: return shift ? '{' : '[';
        case 0x30: return shift ? '}' : ']';
        case 0x31: return shift ? '|' : '\\';
        case 0x33: return shift ? ':' : ';';
        case 0x34: return shift ? '"' : '\'';
        case 0x35: return shift ? '~' : '`';
        case 0x36: return shift ? '<' : ',';
        case 0x37: return shift ? '>' : '.';
        case 0x38: return shift ? '?' : '/';
        default: return 0;
    }
}

static uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

struct Tab5KeyboardInternal {
    QueueHandle_t queue;

    // Modifier state
    bool sym_active;
    bool aa_sticky;
    bool aa_held;
    bool aa_tapped;
    bool ctrl_held;

    // IRQ-driven event gating
    volatile bool irq_pending;
    bool irq_configured;
    gpio_num_t irq_pin;

    // Poll/attach-check throttling (real-time based, since read_key() is called at whatever rate
    // LVGL's indev timer and its own drain-loop - via continue_reading - happen to run at, unlike
    // the old deprecated-HAL's fixed 20ms Timer)
    uint32_t last_poll_ms;
    uint32_t last_attach_check_ms;
    bool was_attached;
    bool pending_attach_state;
    uint8_t pending_attach_confirm_count;

    // Software key-repeat state (tracked by position to survive modifier changes)
    uint32_t repeat_key;
    uint8_t repeat_row;
    uint8_t repeat_col;
    uint32_t repeat_start_ms;
    uint32_t repeat_last_ms;

    Tab5KeyboardAttachListener attach_listener;
    void* attach_listener_context;
};

// ---------------------------------------------------------------------------
// I2C helpers
// ---------------------------------------------------------------------------
static bool read_reg(Device* device, uint8_t reg, uint8_t* value) {
    auto* parent = device_get_parent(device);
    return i2c_controller_read_register(parent, I2C_ADDRESS, reg, value, 1, pdMS_TO_TICKS(50)) == ERROR_NONE;
}

static bool write_reg(Device* device, uint8_t reg, uint8_t value) {
    auto* parent = device_get_parent(device);
    return i2c_controller_write_register(parent, I2C_ADDRESS, reg, &value, 1, pdMS_TO_TICKS(50)) == ERROR_NONE;
}

static bool is_attached_raw(Device* device) {
    auto* parent = device_get_parent(device);
    return i2c_controller_has_device_at_address(parent, I2C_ADDRESS, pdMS_TO_TICKS(100)) == ERROR_NONE;
}

// ---------------------------------------------------------------------------
// LED helpers - LED0 = Sym indicator (green), LED1 = Aa indicator (red)
// RGB register layout: [B, G, R] per LED, stride 4 (byte 3 reserved)
// ---------------------------------------------------------------------------
static void update_leds(Device* device, const Tab5KeyboardInternal* internal) {
    auto* parent = device_get_parent(device);
    // [LED0: B,G,R, reserved, LED1: B,G,R]
    uint8_t buf[7] = {
        0x00, internal->sym_active ? uint8_t(0xA0) : uint8_t(0x00), 0x00, 0x00,
        0x00, 0x00, internal->aa_sticky ? uint8_t(0xA0) : uint8_t(0x00),
    };
    i2c_controller_write_register(parent, I2C_ADDRESS, REG_RGB_BASE, buf, 7, pdMS_TO_TICKS(50));
}

// ---------------------------------------------------------------------------
// IRQ pin - active-low, falling edge
// ---------------------------------------------------------------------------
static void IRAM_ATTR irq_handler(void* arg) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(arg);
    internal->irq_pending = true;
}

static bool configure_irq_pin(Tab5KeyboardInternal* internal) {
    gpio_config_t io_conf{};
    io_conf.pin_bit_mask = (1ULL << internal->irq_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    if (gpio_config(&io_conf) != ESP_OK) {
        return false;
    }

    const esp_err_t svc = gpio_install_isr_service(0);
    if (svc != ESP_OK && svc != ESP_ERR_INVALID_STATE) {
        return false;
    }

    if (gpio_isr_handler_add(internal->irq_pin, irq_handler, internal) != ESP_OK) {
        gpio_set_intr_type(internal->irq_pin, GPIO_INTR_DISABLE);
        return false;
    }

    internal->irq_configured = true;
    return true;
}

static void remove_irq_pin(Tab5KeyboardInternal* internal) {
    if (!internal->irq_configured) {
        return;
    }
    gpio_isr_handler_remove(internal->irq_pin);
    internal->irq_configured = false;
    internal->irq_pending = false;
}

// ---------------------------------------------------------------------------
// drain_events - reads all pending events from the device queue
// ---------------------------------------------------------------------------
static void drain_events(Device* device, Tab5KeyboardInternal* internal) {
    uint8_t count = 0;
    if (!read_reg(device, REG_EVENT_NUM, &count) || count == 0) {
        return;
    }

    while (count > 0) {
        uint8_t raw = 0;
        if (!read_reg(device, REG_KEY_EVENT, &raw) || raw == KEY_EVENT_EMPTY) {
            break;
        }

        const bool pressed = (raw & 0x80U) != 0U;
        const uint8_t row = (raw >> 4U) & 0x07U;
        const uint8_t col = raw & 0x0FU;

        // Modifier keys: update state, no key output
        if (row == MOD_ROW_SYM && col == MOD_COL_SYM) {
            internal->sym_active = pressed;
            update_leds(device, internal);
            count--;
            continue;
        }
        if (row == MOD_ROW_AA && col == MOD_COL_AA) {
            if (pressed) {
                internal->aa_held = true;
                internal->aa_tapped = true; // assume tap until a real key is pressed while held
            } else {
                // Only latch sticky if no non-modifier key was pressed during this hold
                if (internal->aa_tapped) {
                    internal->aa_sticky = !internal->aa_sticky;
                }
                internal->aa_held = false;
                internal->aa_tapped = false;
            }
            update_leds(device, internal);
            count--;
            continue;
        }
        if (row == MOD_ROW_CTRL && col == MOD_COL_CTRL) {
            internal->ctrl_held = pressed;
            count--;
            continue;
        }
        if (row == MOD_ROW_ALT && col == MOD_COL_ALT) {
            count--;
            continue;
        }

        if (row < 5U && col < 14U) {
            const bool aa_active = internal->aa_held || internal->aa_sticky;
            const HidMapping& m = internal->sym_active
                ? KEY_MATRIX_HID_SYM[row * 14U + col]
                : KEY_MATRIX_HID_BASE[row * 14U + col];
            if (m.keycode != 0U) {
                const uint8_t modifier = static_cast<uint8_t>(m.modifier | (aa_active ? 0x02U : 0U));
                const uint32_t lv_key = tab5_translate_key(m.keycode, modifier, internal->ctrl_held);
                if (lv_key != 0U) {
                    if (pressed) {
                        // A real key was pressed — this hold is a chord, not a tap
                        internal->aa_tapped = false;
                        // Note: ESC used to stop the foreground app directly (tt::app::stop()) in
                        // the deprecated-HAL version - that's an app-layer concern the driver has
                        // no business reaching into, so ESC is now just queued as a normal key
                        // like everything else (LVGL/app code already handles ESC via focus/group
                        // navigation the same way a dedicated ESC key on any other keyboard would).
                        xQueueSend(internal->queue, &lv_key, 0);
                        // Arm software repeat tracking by row/col to survive modifier changes
                        const uint32_t now = now_ms();
                        internal->repeat_key = lv_key;
                        internal->repeat_row = row;
                        internal->repeat_col = col;
                        internal->repeat_start_ms = now;
                        internal->repeat_last_ms = 0;
                        // Consume sticky Aa after one keypress
                        if (internal->aa_sticky) {
                            internal->aa_sticky = false;
                            internal->aa_held = false;
                            update_leds(device, internal);
                        }
                    } else if (row == internal->repeat_row && col == internal->repeat_col) {
                        // Match release by position, not translated value — survives sticky Aa clear
                        internal->repeat_key = 0;
                    }
                }
            }
        }
        count--;
    }

    // Clear INT status after draining so the line de-asserts
    write_reg(device, REG_INT_STAT, 0x00);
}

// ---------------------------------------------------------------------------
// reinit_device - (re)applies the device register configuration. Used at start() and again on
// hot-plug reattach, since the device's RGB mode and interrupt configuration are volatile and
// reset to power-on defaults when the keyboard is unplugged and reconnected.
// ---------------------------------------------------------------------------
static void reinit_device(Device* device, Tab5KeyboardInternal* internal) {
    write_reg(device, REG_KEYBOARD_MODE, 0x00); // Normal mode
    write_reg(device, REG_EVENT_NUM, 0x00);     // flush event queue
    write_reg(device, REG_INT_STAT, 0x00);      // clear pending INT
    write_reg(device, REG_RGB_MODE, 0x01);      // Custom RGB mode (manual LED control)
    write_reg(device, REG_BRIGHTNESS, 50);      // 50% brightness
    update_leds(device, internal);              // restore current LED state

    if (internal->irq_configured) {
        write_reg(device, REG_INT_CFG, 0x01);   // re-enable Normal-mode interrupt (bit 0)
    }
}

// ---------------------------------------------------------------------------
// check_attach_state - throttled (~1s) hot-plug detection. Reapplies device register
// configuration on reattach, and notifies the registered attach listener (if any) of confirmed
// transitions - see Tab5KeyboardAttachListener's doc comment for the retry contract.
// ---------------------------------------------------------------------------
static void check_attach_state(Device* device, Tab5KeyboardInternal* internal) {
    uint32_t now = now_ms();
    if (now - internal->last_attach_check_ms < ATTACH_CHECK_INTERVAL_MS) {
        return;
    }
    internal->last_attach_check_ms = now;

    const bool attached = is_attached_raw(device);
    if (attached == internal->was_attached) {
        internal->pending_attach_confirm_count = 0;
        return;
    }

    // Require the new state to be confirmed on a second consecutive check before acting - a
    // single probe on a floating/half-connected bus (e.g. mid-unplug) can false-positive.
    if (attached != internal->pending_attach_state || internal->pending_attach_confirm_count == 0) {
        internal->pending_attach_state = attached;
        internal->pending_attach_confirm_count = 1;
        return;
    }
    internal->pending_attach_confirm_count = 0;

    if (attached) {
        reinit_device(device, internal);
    }

    if (internal->attach_listener != nullptr) {
        if (!internal->attach_listener(device, attached, internal->attach_listener_context)) {
            return; // not handled yet (e.g. LVGL lock busy) - retry on the next confirmed check
        }
    }

    internal->was_attached = attached;
}

// ---------------------------------------------------------------------------
// poll_if_due - the closest equivalent to the old deprecated-HAL's 20ms-Timer-driven
// processKeyboard(): drains new key events (IRQ-gated or polled), ticks software key-repeat, and
// checks hot-plug attach state. Called from read_key(), throttled to real elapsed time rather
// than call count, since read_key() can be called back-to-back multiple times per LVGL indev
// timer tick while draining an already-queued burst (continue_reading).
// ---------------------------------------------------------------------------
static void poll_if_due(Device* device, Tab5KeyboardInternal* internal) {
    uint32_t now = now_ms();
    if (!internal->irq_pending && (now - internal->last_poll_ms) < POLL_INTERVAL_MS) {
        return;
    }
    internal->last_poll_ms = now;

    bool should_drain = false;
    if (internal->irq_configured) {
        if (internal->irq_pending) {
            internal->irq_pending = false;
            should_drain = true;
        }
    } else {
        // Polling: check INT_STAT first — bit 0 = Normal mode event pending
        uint8_t status = 0;
        if (read_reg(device, REG_INT_STAT, &status) && (status & 0x01U)) {
            should_drain = true;
        }
    }
    if (should_drain) {
        drain_events(device, internal);
    }

    // Software key-repeat (runs every tick regardless of IRQ)
    if (internal->repeat_key != 0U) {
        if ((now - internal->repeat_start_ms) >= REPEAT_INITIAL_MS) {
            const uint32_t last = internal->repeat_last_ms;
            if (last == 0 || (now - last) >= REPEAT_RATE_MS) {
                internal->repeat_last_ms = now;
                xQueueSend(internal->queue, &internal->repeat_key, 0);
            }
        }
    }

    check_attach_state(device, internal);
}

static gpio_num_t pin_or_nc(const GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

// region Driver lifecycle

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Tab5KeyboardInternal*>(malloc(sizeof(Tab5KeyboardInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    memset(internal, 0, sizeof(Tab5KeyboardInternal));

    internal->queue = xQueueCreate(20, sizeof(uint32_t));
    if (internal->queue == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    internal->repeat_row = 0xFF;
    internal->repeat_col = 0xFF;

    internal->irq_pin = pin_or_nc(config->pin_interrupt);
    if (internal->irq_pin != GPIO_NUM_NC) {
        configure_irq_pin(internal); // best-effort; falls back to polling if it fails. Must
                                     // happen before reinit_device() so REG_INT_CFG is written
                                     // if IRQ setup succeeded.
    }

    // Best-effort: if the keyboard isn't attached yet (e.g. this device is constructed
    // speculatively at boot so it can be hot-plug-detected later), these I2C writes fail
    // silently and reinit_device() runs again once attach is detected.
    reinit_device(device, internal);
    internal->was_attached = is_attached_raw(device);

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));

    remove_irq_pin(internal);
    write_reg(device, REG_INT_CFG, 0x00); // disable all interrupts
    internal->sym_active = false;
    internal->aa_sticky = false;
    internal->aa_held = false;
    update_leds(device, internal); // turn LEDs off

    vQueueDelete(internal->queue);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static error_t tab5_keyboard_read_key(Device* device, KeyboardKeyData* data) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));

    poll_if_due(device, internal);

    uint32_t lv_key = 0;
    if (xQueueReceive(internal->queue, &lv_key, 0) == pdTRUE) {
        data->key = lv_key;
        data->pressed = true;
        data->continue_reading = uxQueueMessagesWaiting(internal->queue) > 0;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }

    return ERROR_NONE;
}

// endregion

static const KeyboardApi tab5_keyboard_api = {
    .read_key = tab5_keyboard_read_key,
};

// Defined in module.cpp - this driver is registered directly by m5stack-tab5's own module,
// not a separate Drivers/ module.
extern Module m5stack_tab5_module;

Driver tab5_keyboard_driver = {
    .name = "tab5-keyboard",
    .compatible = (const char*[]) { "m5stack,tab5-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &tab5_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &m5stack_tab5_module,
    .internal = nullptr
};

// region Attach listener

void tab5_keyboard_add_attach_listener(Device* device, Tab5KeyboardAttachListener callback, void* context) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));
    if (internal->attach_listener != nullptr) {
        LOG_W(TAG, "Replacing existing attach listener without it being removed first");
    }
    internal->attach_listener = callback;
    internal->attach_listener_context = context;
}

void tab5_keyboard_remove_attach_listener(Device* device, Tab5KeyboardAttachListener callback) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));
    if (internal->attach_listener != callback) {
        return;
    }
    internal->attach_listener = nullptr;
    internal->attach_listener_context = nullptr;
}

// endregion

// region Dynamic construction

// Reacts to the Tab5 keyboard accessory's hot-plug attach state (see Tab5KeyboardAttachListener's
// doc comment above for the retry contract). This is UI-layer behavior the driver itself can't do
// (it has no LVGL dependency): switch to landscape while the keyboard is attached, restoring
// whatever rotation was active before once it's removed - but only if the user hasn't manually
// changed it since attaching, in which case their choice is respected. Ported as-is from the
// deprecated HAL's Tab5Keyboard::applyAutoRotation().
static bool on_keyboard_attach_changed(Device* /*device*/, bool attached, void* /*context*/) {
    static lv_display_rotation_t saved_rotation = LV_DISPLAY_ROTATION_0;
    static bool rotation_override_active = false;

    auto* display = lv_display_get_default();
    if (display == nullptr) {
        return false; // LVGL not ready yet - retry on the next confirmed check
    }

    if (!lvgl_try_lock(pdMS_TO_TICKS(1000))) {
        return false; // retry next check
    }

    if (attached) {
        if (lv_display_get_rotation(display) != LV_DISPLAY_ROTATION_90) {
            saved_rotation = lv_display_get_rotation(display);
            rotation_override_active = true;
            lv_display_set_rotation(display, LV_DISPLAY_ROTATION_90);
        }
    } else {
        // Only restore if rotation is still what we set it to - if the user manually changed it
        // since attaching, respect their choice instead.
        if (rotation_override_active && lv_display_get_rotation(display) == LV_DISPLAY_ROTATION_90) {
            lv_display_set_rotation(display, saved_rotation);
        }
        rotation_override_active = false;
    }

    lvgl_unlock();
    return true;
}

static Tab5KeyboardConfig tab5_keyboard_config {};
static Device tab5_keyboard_device {};

// The keyboard accessory is a kernel driver device (m5stack,tab5-keyboard, defined directly in
// this project). Unlike the display/touch, it isn't gated on the display-variant detection at
// all (it lives on i2c2, a separate bus) - lvgl-module binds its indev unconditionally at boot
// regardless of physical attach state, and the driver's own read_key() polling handles hot-plug
// internally.
void tab5_create_keyboard(Device* i2c2) {
    tab5_keyboard_device = Device {
        .address = 0,
        .name = "keyboard0",
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr,
    };

    GpioPinSpec pin_interrupt = GPIO_PIN_SPEC_NONE;
    Device* gpio0 = nullptr;
    if (device_get_by_name("gpio0", &gpio0) == ERROR_NONE) {
        pin_interrupt = GpioPinSpec { gpio0, 50, GPIO_FLAG_NONE };
        device_put(gpio0);
    } else {
        LOG_W(TAG, "display_detect: gpio0 not found, keyboard will fall back to INT_STAT polling");
    }

    tab5_keyboard_config = Tab5KeyboardConfig {
        .address = 0x6D,
        .pin_interrupt = pin_interrupt,
    };
    tab5_keyboard_device.config = &tab5_keyboard_config;

    // Parented to i2c2 itself (not root, unlike the display): the keyboard driver's start() uses
    // device_get_parent() as its I2C bus controller.
    if (construct_add_start(&tab5_keyboard_device, i2c2, "m5stack,tab5-keyboard")) {
        tab5_keyboard_add_attach_listener(&tab5_keyboard_device, on_keyboard_attach_changed, nullptr);
    }
}

// endregion
