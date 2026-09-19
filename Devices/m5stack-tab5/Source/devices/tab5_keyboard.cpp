// SPDX-License-Identifier: Apache-2.0
#include "tab5_keyboard.h"

#include "devices_common.h"

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <driver/gpio.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <cstdlib>
#include <cstring>

constexpr auto* TAG = "Tab5Keyboard";
#define GET_CONFIG(device) (static_cast<const Tab5KeyboardConfig*>((device)->config))

static constexpr uint8_t I2C_ADDRESS = 0x6D;

// Software key-repeat timing
static constexpr uint32_t REPEAT_INITIAL_MS = 400;
static constexpr uint32_t REPEAT_RATE_MS = 80;

// I2C event-poll interval
// Drives both REG_INT_STAT polling (when no IRQ pin) and software key-repeat ticking.
static constexpr uint32_t POLL_INTERVAL_MS = 20;

// Upper bound on events consumed per drain_events() call. Since the loop re-reads REG_EVENT_NUM
// each iteration rather than counting down a latched value, this caps the damage if the device
// ever reports a non-zero count that never drains - without it, that would spin forever holding
// the I2C bus. The device's own queue is far smaller than this, so it never limits normal bursts.
static constexpr uint8_t MAX_EVENTS_PER_DRAIN = 32;

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
    // Row 0: Esc F1 F2 F3 F4 F5 F6 F7 F8 F9 F10 F11 F12 Del
    {0x29, 0x00}, {0x3A, 0x00}, {0x3B, 0x00}, {0x3C, 0x00}, {0x3D, 0x00}, {0x3E, 0x00},
    {0x3F, 0x00}, {0x40, 0x00}, {0x41, 0x00}, {0x42, 0x00}, {0x43, 0x00}, {0x44, 0x00},
    {0x45, 0x00}, {0x4C, 0x00},
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
// HID usage code + modifier → key (Unicode codepoint)
// Covers all codes present in the Tab5 matrix tables above. Every key returns a real Unicode
// codepoint per KeyboardKeyData::key's contract - never an LV_KEY_* constant. Keys with no
// ordinary character of their own (arrows, Tab/Shift+Tab and Ctrl+arrow as focus nav) use the
// CodePoint enum's standard Unicode symbols for the concept (CODEPOINT_FOCUS_PREV/NEXT reused for
// Ctrl+arrow's focus-move aliases). lvgl-module's keyboard.cpp translates all of these back to
// LVGL's own sentinels (CODEPOINT_ESCAPE/BACKSPACE/DELETE already equal their LV_KEY_* counterpart
// numerically, so no translation is needed for those).
//
// Ctrl chords on ordinary keys are NOT folded into the returned value - the C0 control codes a
// terminal wants (Ctrl+C = 0x03, Ctrl+K = 0x0B, ...) would collide with a codepoint-based scheme,
// so Ctrl is reported out-of-band via KeyboardKeyData::ctrl instead and consumers that want
// control codes derive them themselves.
// ---------------------------------------------------------------------------
static uint32_t tab5_translate_key(uint8_t keycode, uint8_t modifier, bool ctrl) {
    const bool shift = (modifier & 0x22U) != 0U;

    // Navigation → key (Unicode codepoint)
    switch (keycode) {
        case 0x29: return CODEPOINT_ESCAPE;
        case 0x28: return CODEPOINT_ENTER;
        case 0x2A: return CODEPOINT_BACKSPACE;
        case 0x4C: return CODEPOINT_DELETE;
        case 0x2B: return CODEPOINT_TAB;
        case 0x52: return CODEPOINT_ARROW_UP;
        case 0x51: return CODEPOINT_ARROW_DOWN;
        case 0x50: return CODEPOINT_ARROW_LEFT;
        case 0x4F: return CODEPOINT_ARROW_RIGHT;
        default: break;
    }

    // F1-F12 (Sym layer over the number row) have no Unicode/LVGL representation - callers that
    // want them read KeyboardKeyData::hid_keycode instead (see drain_events()), which is
    // populated for every key regardless of whether `key` itself has a meaningful value.

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

// Queued key event. Modifier state is captured here at enqueue time rather than read back from
// Tab5KeyboardInternal at dequeue time, since the user can release Ctrl before read_key() drains
// the event - and software key-repeat replays this same struct, so a held chord keeps its modifiers.
struct Tab5KeyEvent {
    uint32_t key;
    bool pressed;
    bool ctrl;
    bool alt;
    uint8_t hid_keycode;
    uint8_t hid_modifier;
};

struct Tab5KeyboardInternal {
    QueueHandle_t queue;

    // Modifier state
    bool sym_active;
    bool aa_sticky;
    bool aa_held;
    bool aa_tapped;
    bool ctrl_held;
    bool alt_held;

    // IRQ-driven event gating
    volatile bool irq_pending;
    bool irq_configured;
    gpio_num_t irq_pin;

    // Poll throttling (real-time based, since read_key() is called at whatever rate LVGL's indev
    // timer and its own drain-loop, via continue_reading, happen to run at).
    uint32_t last_poll_ms;

    // Original press event for every currently-held key, indexed by matrix position (row*14+col),
    // so a release can recover the exact event its press queued (modifiers captured at press
    // time) even when another key was pressed and released in between. held_event[i].key can
    // legitimately be 0 (e.g. F1-F12, see drain_events()), so held[i] tracks validity separately
    // rather than using a sentinel key value.
    Tab5KeyEvent held_event[70];
    bool held[70];

    // Software key-repeat state: tracks only the most recently pressed key, independent of the
    // per-position storage above (repeats stop as soon as a different key is pressed, matching
    // typical keyboard behavior, and don't need to survive that key's release).
    Tab5KeyEvent repeat_event;
    uint8_t repeat_row;
    uint8_t repeat_col;
    uint32_t repeat_start_ms;
    uint32_t repeat_last_ms;
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

// Short-timeout variant used only by tab5_keyboard_reinit(), which runs on the FreeRTOS timer
// daemon task (via tab5_keyboard_attach_detect.cpp) - a slow/absent device there blocks every
// other software timer in the system, not just this one, so it can't afford write_reg()'s 50ms
// per-call budget. read_reg()/write_reg() themselves stay at 50ms since they're also used from the
// hot IRQ/poll path (drain_events()), where a too-short timeout would cause missed key events
// under normal bus contention.
static bool write_reg_fast(Device* device, uint8_t reg, uint8_t value) {
    auto* parent = device_get_parent(device);
    return i2c_controller_write_register(parent, I2C_ADDRESS, reg, &value, 1, pdMS_TO_TICKS(2)) == ERROR_NONE;
}

static error_t probe(Device* device) {
    auto* parent = device_get_parent(device);
    return i2c_controller_has_device_at_address(parent, I2C_ADDRESS, pdMS_TO_TICKS(5));
}

error_t tab5_keyboard_is_attached(Device* device) {
    return device_is_ready(device);
}

// ---------------------------------------------------------------------------
// LED helpers - LED0 = Sym indicator (blue), LED1 = Aa indicator (red)
// RGB register layout: [B, G, R] per LED, stride 4 (byte 3 reserved)
// ---------------------------------------------------------------------------
static constexpr uint8_t LED_SYM_ON_BLUE = 0xC0;
static constexpr uint8_t LED_AA_ON_RED = 0x90;

static void update_leds(Device* device, Tab5KeyboardInternal* internal) {
    auto* parent = device_get_parent(device);
    uint8_t buf[7] = {
        internal->sym_active ? LED_SYM_ON_BLUE : uint8_t(0x00), 0x00, 0x00, 0x00,
        0x00, 0x00, internal->aa_sticky ? LED_AA_ON_RED : uint8_t(0x00),
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
    // REG_EVENT_NUM is re-read every iteration rather than latched once and counted down, matching
    // M5's own UnitTab5Keyboard::drain_events(). Each REG_KEY_EVENT read consumes one event from the
    // device queue, so a count latched up front can go stale mid-drain; re-reading makes the loop
    // self-correcting and lets it stop as soon as the device says the queue is actually empty.
    uint8_t drained = 0;
    while (drained < MAX_EVENTS_PER_DRAIN) {
        uint8_t count = 0;
        if (!read_reg(device, REG_EVENT_NUM, &count) || count == 0) {
            break;
        }

        uint8_t raw = 0;
        if (!read_reg(device, REG_KEY_EVENT, &raw) || raw == KEY_EVENT_EMPTY) {
            break;
        }
        drained++;

        const bool pressed = (raw & 0x80U) != 0U;
        const uint8_t row = (raw >> 4U) & 0x07U;
        const uint8_t col = raw & 0x0FU;

        // Modifier keys: update state, no key output
        if (row == MOD_ROW_SYM && col == MOD_COL_SYM) {
            internal->sym_active = pressed;
            update_leds(device, internal);
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
            continue;
        }
        if (row == MOD_ROW_CTRL && col == MOD_COL_CTRL) {
            internal->ctrl_held = pressed;
            continue;
        }
        if (row == MOD_ROW_ALT && col == MOD_COL_ALT) {
            internal->alt_held = pressed;
            continue;
        }

        if (row < 5U && col < 14U) {
            const bool aa_active = internal->aa_held || internal->aa_sticky;
            const HidMapping& m = internal->sym_active
                ? KEY_MATRIX_HID_SYM[row * 14U + col]
                : KEY_MATRIX_HID_BASE[row * 14U + col];
            if (m.keycode != 0U) {
                uint8_t modifier = static_cast<uint8_t>(m.modifier | (aa_active ? 0x02U : 0U));
                if (internal->ctrl_held) modifier |= 0x01U; // HID LeftCtrl
                if (internal->alt_held) modifier |= 0x04U;  // HID LeftAlt
                const uint32_t lv_key = tab5_translate_key(m.keycode, modifier, internal->ctrl_held);
                // Queue whenever there's a HID keycode, even if this key has no Unicode/LVGL
                // representation (e.g. F1-F12) - lv_key stays 0 for those, callers that only
                // care about the codepoint ignore a 0 key the same way they always have.
                if (pressed) {
                    // A real key was pressed — this hold is a chord, not a tap
                    internal->aa_tapped = false;
                    // Note: ESC used to stop the foreground app directly (tt::app::stop()) in
                    // the deprecated-HAL version - that's an app-layer concern the driver has
                    // no business reaching into, so ESC is now just queued as a normal key
                    // like everything else (LVGL/app code already handles ESC via focus/group
                    // navigation the same way a dedicated ESC key on any other keyboard would).
                    const Tab5KeyEvent event = { lv_key, true, internal->ctrl_held, internal->alt_held,
                                                 m.keycode, modifier };
                    xQueueSend(internal->queue, &event, 0);
                    // Remember this key's press event by position so its release (whenever it
                    // comes, regardless of what else is pressed in between) reports the same value.
                    const uint8_t idx = row * 14U + col;
                    internal->held_event[idx] = event;
                    internal->held[idx] = true;
                    // Arm software repeat tracking by row/col to survive modifier changes
                    const uint32_t now = now_ms();
                    internal->repeat_event = event;
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
                } else {
                    // Always queue the release: callers key their own "is this held" state off
                    // (key, pressed) pairs, and a dropped release leaves that key stuck down forever.
                    //
                    // Reuse the matching press's key/modifier (via held_event, tracked by matrix
                    // position) rather than recomputing from current modifier state: aa_active
                    // above is read fresh, but sticky Aa is consumed right after the press fires
                    // (above), so recomputing here would give the release a different key value
                    // than its press whenever Aa was sticky (e.g. shifted vs unshifted). Only
                    // recompute as a fallback for the case where no press was ever recorded for
                    // this position (e.g. driver just started while the key was already held).
                    const uint8_t idx = row * 14U + col;
                    Tab5KeyEvent event;
                    if (internal->held[idx]) {
                        event = internal->held_event[idx];
                        internal->held[idx] = false;
                    } else {
                        event = { lv_key, false, internal->ctrl_held, internal->alt_held, m.keycode, modifier };
                    }
                    event.pressed = false;
                    xQueueSend(internal->queue, &event, 0);

                    if (row == internal->repeat_row && col == internal->repeat_col) {
                        internal->repeat_event.key = 0;
                    }
                }
            }
        }
    }

    // Clear INT status after draining so the line de-asserts
    write_reg(device, REG_INT_STAT, 0x00);
}

// ---------------------------------------------------------------------------
// tab5_keyboard_reinit - (re)applies the device register configuration. Called from start() and
// again by tab5_keyboard_attach_detect.cpp on confirmed hot-plug reattach, since the device's RGB
// mode and interrupt configuration are volatile and reset to power-on defaults when the keyboard
// is unplugged and reconnected.
// ---------------------------------------------------------------------------
static void tab5_keyboard_reinit(Device* device) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));
    write_reg_fast(device, REG_KEYBOARD_MODE, 0x00); // Normal mode
    write_reg_fast(device, REG_EVENT_NUM, 0x00);     // flush event queue
    write_reg_fast(device, REG_INT_STAT, 0x00);      // clear pending INT
    write_reg_fast(device, REG_RGB_MODE, 0x01);      // Custom RGB mode (manual LED control)
    write_reg_fast(device, REG_BRIGHTNESS, 30);      // 30% brightness
    update_leds(device, internal);                   // restore current LED state

    if (internal->irq_configured) {
        write_reg_fast(device, REG_INT_CFG, 0x01);   // re-enable Normal-mode interrupt (bit 0)
    }
}

static void tab5_keyboard_reset_state(Device* device) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));

    for (uint8_t idx = 0; idx < 70U; idx++) {
        if (internal->held[idx]) {
            Tab5KeyEvent event = internal->held_event[idx];
            event.pressed = false;
            // This runs under lvgl_lock() (apply_state()'s caller), and the consumer that drains
            // this queue is LVGL's own indev read callback - blocking here for queue space could
            // deadlock against that consumer needing the same lock. Best-effort send only, same as
            // drain_events()'s hot path; internal->held[idx] is still cleared on a dropped send so
            // this reset doesn't get stuck retrying a release that already lost its only chance to
            // be delivered before the state it referred to (an unplugged keyboard) is gone anyway.
            xQueueSend(internal->queue, &event, 0);
            internal->held[idx] = false;
        }
    }

    internal->repeat_event.key = 0;
    internal->repeat_row = 0xFF;
    internal->repeat_col = 0xFF;

    internal->sym_active = false;
    internal->aa_sticky = false;
    internal->aa_held = false;
    internal->aa_tapped = false;
    internal->ctrl_held = false;
    internal->alt_held = false;
    update_leds(device, internal);
}

// ---------------------------------------------------------------------------
// poll_if_due - drains new key events (IRQ-gated or polled) and ticks software key-repeat.
// Called from read_key(), throttled to real elapsed time rather than call count, since read_key()
// can be called back-to-back multiple times per LVGL indev timer tick while draining an
// already-queued burst (continue_reading). Hot-plug attach detection lives outside the driver -
// see tab5_keyboard_attach_detect.cpp.
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

    // Software key-repeat (runs every tick regardless of IRQ).
    //
    // The clock is re-read here rather than reusing `now` from the top of the function: a press
    // handled by the drain above sets repeat_start_ms to a timestamp taken *during* the drain, which
    // is later than `now`. The unsigned subtraction below would then wrap to a huge value and clear
    // the REPEAT_INITIAL_MS gate immediately, emitting one spurious repeat ~1ms after every press.
    const uint32_t repeat_now = now_ms();
    if (internal->repeat_event.key != 0U) {
        if ((repeat_now - internal->repeat_start_ms) >= REPEAT_INITIAL_MS) {
            const uint32_t last = internal->repeat_last_ms;
            if (last == 0 || (repeat_now - last) >= REPEAT_RATE_MS) {
                internal->repeat_last_ms = repeat_now;
                xQueueSend(internal->queue, &internal->repeat_event, 0);
            }
        }
    }
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

    internal->queue = xQueueCreate(20, sizeof(Tab5KeyEvent));
    if (internal->queue == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    internal->repeat_row = 0xFF;
    internal->repeat_col = 0xFF;

    internal->irq_pin = pin_or_nc(config->pin_interrupt);
    if (internal->irq_pin != GPIO_NUM_NC) {
        configure_irq_pin(internal); // best-effort; falls back to polling if it fails. Must
                                     // happen before tab5_keyboard_reinit() so REG_INT_CFG is
                                     // written if IRQ setup succeeded.
    }

    // Driver data must be set before tab5_keyboard_reinit() - it looks internal back up via
    // device_get_driver_data().
    device_set_driver_data(device, internal);

    // Driver::probe (tab5_keyboard_is_attached) already confirmed presence before driver_bind()
    // called this.
    tab5_keyboard_reinit(device);

    return ERROR_NONE;
}

// How long to give LVGL's own indev timer to drain the release events tab5_keyboard_reset_state()
// pushes below before the queue carrying them is deleted - longer than LVGL's default indev poll
// period so a key held across an unplug doesn't read as stuck forever.
static constexpr uint32_t STOP_DRAIN_GRACE_MS = 50;

static error_t stop(Device* device) {
    auto* internal = static_cast<Tab5KeyboardInternal*>(device_get_driver_data(device));

    remove_irq_pin(internal);
    write_reg(device, REG_INT_CFG, 0x00); // disable all interrupts

    tab5_keyboard_reset_state(device);
    vTaskDelay(pdMS_TO_TICKS(STOP_DRAIN_GRACE_MS));

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

    Tab5KeyEvent event = {};
    if (xQueueReceive(internal->queue, &event, 0) == pdTRUE) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = uxQueueMessagesWaiting(internal->queue) > 0;
        data->ctrl = event.ctrl;
        data->alt = event.alt;
        data->hid_keycode = event.hid_keycode;
        data->hid_modifier = event.hid_modifier;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
        data->ctrl = false;
        data->alt = false;
        data->hid_keycode = 0;
        data->hid_modifier = 0;
    }

    return ERROR_NONE;
}

// endregion

static const KeyboardApi tab5_keyboard_api = {
    .read_key = tab5_keyboard_read_key,
    .get_backlight = nullptr,
};

// Defined in module.cpp - this driver is registered directly by m5stack-tab5's own module,
// not a separate Drivers/ module.
extern Module m5stack_tab5_module;

Driver tab5_keyboard_driver = {
    .name = "tab5-keyboard",
    .compatible = (const char*[]) { "m5stack,tab5-keyboard", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = probe,
    .api = &tab5_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &m5stack_tab5_module,
    .internal = nullptr
};

// region Dynamic construction

static Tab5KeyboardConfig tab5_keyboard_config {};
static Device tab5_keyboard_device {};

// The keyboard accessory is a kernel driver device (m5stack,tab5-keyboard, defined directly in
// this project). Unlike the display/touch, it isn't gated on the display-variant detection at
// all (it lives on i2c2, a separate bus). Registered with the kernel's hotplug poller instead of
// started here - Driver::probe (tab5_keyboard_is_attached) decides when it starts/stops. LVGL
// rotation-on-attach lives in tab5_keyboard_attach_detect.cpp, not this driver - see module.cpp
// for where that gets started.
void tab5_create_keyboard(Device* i2c2) {
    tab5_keyboard_device = Device {
        .address = 0,
        .name = "keyboard0",
        .config = nullptr,
        .parent = nullptr,
        .flags = {},
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
    if (construct_add(&tab5_keyboard_device, i2c2, "m5stack,tab5-keyboard")) {
        device_hotplug_register(&tab5_keyboard_device);
    }
}

// endregion
