// SPDX-License-Identifier: Apache-2.0
#include <drivers/tca8418.h>

#include <tca8418_module.h>

#include <tactility/check.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <cstdlib>

#define TAG "TCA8418"
#define GET_CONFIG(device) (static_cast<const Tca8418Config*>((device)->config))

static const TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(50);

// TCA8418 register map (https://www.ti.com/lit/ds/symlink/tca8418.pdf).
static constexpr uint8_t REG_CFG = 0x01;
static constexpr uint8_t REG_KEY_EVENT_A = 0x04;
static constexpr uint8_t REG_KP_GPIO1 = 0x1D;
static constexpr uint8_t REG_KP_GPIO2 = 0x1E;
static constexpr uint8_t REG_KP_GPIO3 = 0x1F;

// The KEY_EVENT register always encodes a key's position as row*10+col+1, using a fixed
// 10-column addressing space regardless of how many columns are actually wired/enabled (a
// TCA8418 hardware constant, not related to Tca8418Config::columns).
static constexpr uint8_t KEY_EVENT_ENCODING_WIDTH = 10;

static constexpr uint8_t NO_MODIFIER = 255;

// Worst case per read_key() poll: 16 drained hardware events, each a char key producing a
// press+release pair.
static constexpr uint8_t PENDING_CAPACITY = 32;

struct Tca8418PendingEvent {
    uint32_t key;
    bool pressed;
};

struct Tca8418Internal {
    bool shift_pressed;
    bool sym_pressed;
    bool cap_toggle;
    bool cap_toggle_armed;
    Tca8418PendingEvent pending[PENDING_CAPACITY];
    uint8_t pending_head;
    uint8_t pending_count;
};

static void push_pending(Tca8418Internal* internal, uint32_t key, bool pressed) {
    if (internal->pending_count >= PENDING_CAPACITY) {
        LOG_W(TAG, "Pending event queue full, dropping event");
        return;
    }
    uint8_t tail = (internal->pending_head + internal->pending_count) % PENDING_CAPACITY;
    internal->pending[tail] = { .key = key, .pressed = pressed };
    internal->pending_count++;
}

static bool pop_pending(Tca8418Internal* internal, Tca8418PendingEvent* out_event) {
    if (internal->pending_count == 0) {
        return false;
    }
    *out_event = internal->pending[internal->pending_head];
    internal->pending_head = (internal->pending_head + 1) % PENDING_CAPACITY;
    internal->pending_count--;
    return true;
}

// region Driver lifecycle

// Ported from Adafruit_TCA8418::matrix() (see Drivers/TCA8418's now-removed deprecated-HAL
// equivalent / https://github.com/adafruit/Adafruit_TCA8418).
static bool init_matrix(Device* i2c, uint8_t address, uint8_t rows, uint8_t columns) {
    if (rows > 8 || columns > 10) {
        return false;
    }
    if (rows == 0 || columns == 0) {
        return true;
    }

    uint8_t mask = 0;
    for (int r = 0; r < rows; r++) {
        mask = static_cast<uint8_t>((mask << 1) | 1);
    }
    if (i2c_controller_register8_set(i2c, address, REG_KP_GPIO1, mask, I2C_TIMEOUT) != ERROR_NONE) {
        return false;
    }

    mask = 0;
    for (int c = 0; c < columns && c < 8; c++) {
        mask = static_cast<uint8_t>((mask << 1) | 1);
    }
    if (i2c_controller_register8_set(i2c, address, REG_KP_GPIO2, mask, I2C_TIMEOUT) != ERROR_NONE) {
        return false;
    }

    if (columns > 8) {
        mask = (columns == 9) ? 0x01 : 0x03;
        if (i2c_controller_register8_set(i2c, address, REG_KP_GPIO3, mask, I2C_TIMEOUT) != ERROR_NONE) {
            return false;
        }
    }
    return true;
}

// Some boards route this chip's reset line through an external GPIO/IO-expander pin that must
// be released before the chip will respond (see Tca8418Config::pin_reset).
static void perform_hardware_reset(const Tca8418Config* config) {
    if (config->pin_reset.gpio_controller == nullptr) {
        return;
    }

    // Not requesting GPIO_FLAG_ACTIVE_LOW here: some GPIO expanders (e.g. xl9555/tca95xx/
    // tca9534) can only invert what's read back from an input, not what's driven on an
    // output, so they reject ACTIVE_LOW combined with OUTPUT. Drive the raw physical level
    // instead: this pin's reset line is active-low, so low asserts and high releases.
    auto* rst = gpio_descriptor_acquire(config->pin_reset.gpio_controller, config->pin_reset.pin, GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO);
    if (rst == nullptr) {
        return;
    }

    gpio_descriptor_set_level(rst, false);
    delay_millis(20);
    gpio_descriptor_set_level(rst, true);
    gpio_descriptor_release(rst);
    delay_millis(60);
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &I2C_CONTROLLER_TYPE);

    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<Tca8418Internal*>(calloc(1, sizeof(Tca8418Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->cap_toggle_armed = true;

    perform_hardware_reset(config);

    if (!init_matrix(parent, config->address, config->rows, config->columns)) {
        LOG_E(TAG, "Failed to configure keypad matrix");
        free(internal);
        return ERROR_RESOURCE;
    }

    // BIT7 AI=1 auto-increment, BIT6 GPI_E_CFG=0, BIT5 OVR_FLOW_M=0 (overflow data lost), BIT4
    // INT_CFG=1, others 0: 0x99. Matches the vendor factory firmware / Adafruit_TCA8418 default.
    if (i2c_controller_register8_set(parent, config->address, REG_CFG, 0x99, I2C_TIMEOUT) != ERROR_NONE) {
        LOG_E(TAG, "Failed to configure keypad controller");
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Tca8418Internal*>(device_get_driver_data(device));
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region KeyboardApi

static uint8_t keymap_lookup(const uint8_t* keymap, uint32_t keymap_length, uint8_t row, uint8_t vcol, uint8_t columns) {
    uint32_t index = static_cast<uint32_t>(row) * columns + vcol;
    if (keymap == nullptr || index >= keymap_length) {
        return 0;
    }
    return keymap[index];
}

static void handle_key_event(Tca8418Internal* internal, const Tca8418Config* config, uint8_t event) {
    bool key_down = (event & 0x80) != 0;
    uint8_t code = static_cast<uint8_t>((event & 0x7F) - 1);
    uint8_t row = static_cast<uint8_t>(code / KEY_EVENT_ENCODING_WIDTH);
    uint8_t col = static_cast<uint8_t>(code % KEY_EVENT_ENCODING_WIDTH);
    uint8_t vcol = config->reverse_columns ? static_cast<uint8_t>(config->columns - 1 - col) : col;

    bool is_shift = row == config->shift_row && vcol == config->shift_col;
    bool is_sym = row == config->sym_row && vcol == config->sym_col;

    if (!key_down) {
        // Release event: only modifier state cares about releases.
        if (is_shift) {
            internal->shift_pressed = false;
        }
        if (is_sym) {
            internal->sym_pressed = false;
        }
    } else {
        if (is_shift) {
            internal->shift_pressed = true;
        } else if (is_sym) {
            internal->sym_pressed = true;
        } else {
            const uint8_t* keymap = config->keymap_lc;
            uint32_t keymap_length = config->keymap_lc_length;
            if (internal->sym_pressed) {
                keymap = config->keymap_sy;
                keymap_length = config->keymap_sy_length;
            } else if (internal->shift_pressed || internal->cap_toggle) {
                keymap = config->keymap_uc;
                keymap_length = config->keymap_uc_length;
            }
            uint8_t chr = keymap_lookup(keymap, keymap_length, row, vcol, config->columns);
            if (chr != 0) {
                // LVGL only registers a key on a RELEASED->PRESSED edge, so every keystroke is
                // queued as an immediate press+release pair.
                push_pending(internal, chr, true);
                push_pending(internal, chr, false);
            }
        }

        if (internal->sym_pressed && internal->shift_pressed && internal->cap_toggle_armed &&
            config->sym_row != NO_MODIFIER && config->shift_row != NO_MODIFIER) {
            internal->cap_toggle = !internal->cap_toggle;
            internal->cap_toggle_armed = false;
        }
    }

    if (!internal->sym_pressed && !internal->shift_pressed) {
        internal->cap_toggle_armed = true;
    }
}

static error_t tca8418_read_key(Device* device, KeyboardKeyData* data) {
    auto* internal = static_cast<Tca8418Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);
    auto* parent = device_get_parent(device);

    // Each register read pops one event from the TCA8418's FIFO. Drain it fully per call
    // (bounded, in case of a misbehaving bus) so a fast typing burst isn't throttled to one
    // event per LVGL indev poll period.
    for (int drained = 0; drained < 16; drained++) {
        uint8_t event = 0;
        if (i2c_controller_register8_get(parent, config->address, REG_KEY_EVENT_A, &event, I2C_TIMEOUT) != ERROR_NONE) {
            break;
        }
        if (event == 0) {
            break;
        }
        handle_key_event(internal, config, event);
    }

    Tca8418PendingEvent event;
    if (pop_pending(internal, &event)) {
        data->key = event.key;
        data->pressed = event.pressed;
        data->continue_reading = internal->pending_count > 0;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }
    return ERROR_NONE;
}

// endregion

static constexpr KeyboardApi tca8418_api = {
    .read_key = tca8418_read_key,
};

Driver tca8418_driver = {
    .name = "tca8418",
    .compatible = (const char*[]) { "ti,tca8418", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &tca8418_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &tca8418_module,
    .internal = nullptr
};
