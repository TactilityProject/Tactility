// SPDX-License-Identifier: Apache-2.0
#include <drivers/st7789_soft_i8080.h>
#include <st7789_soft_i8080_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <driver/gpio.h>
#include <soc/gpio_struct.h>

#include <freertos/task.h>

#include <cstdlib>
#include <initializer_list>

#define TAG "ST7789SOFT"
#define GET_CONFIG(device) (static_cast<const St7789SoftI8080Config*>((device)->config))

// Bit-banged 8-bit parallel ST7789 transport.
struct St7789SoftI8080Internal {
    // GPIO register bit masks per pin.
    struct MaskPair {
        uint32_t low; // GPIO0..31 bits
        uint32_t high; // GPIO32..39 bits
    };

    MaskPair set_mask_by_byte[256];
    MaskPair clear_mask_by_byte[256];
    MaskPair cs_mask;
    MaskPair dc_mask;
    MaskPair wr_mask;

    uint8_t madctl;
    int16_t gap_x;
    int16_t gap_y;

    // pin descriptors (8 data + CS/DC/WR/RD), kept for release on stop().
    struct GpioDescriptor* descriptors[12];
    uint8_t descriptor_count;
};

static St7789SoftI8080Internal::MaskPair bitbang_mask_for_pin(gpio_pin_t pin) {
    St7789SoftI8080Internal::MaskPair pair = { 0, 0 };
    if (pin < 32) {
        pair.low = 1U << pin;
    } else {
        pair.high = 1U << (pin - 32);
    }
    return pair;
}

static error_t bitbang_init_pins(St7789SoftI8080Internal* internal, const St7789SoftI8080Config* config) {
    const struct GpioPinSpec* data_specs[8] = {
        &config->pin_d0, &config->pin_d1, &config->pin_d2, &config->pin_d3,
        &config->pin_d4, &config->pin_d5, &config->pin_d6, &config->pin_d7,
    };
    const struct GpioPinSpec* control_specs[4] = {
        &config->pin_cs, &config->pin_dc, &config->pin_wr, &config->pin_rd,
    };

    internal->descriptor_count = 0;
    const auto acquire = [internal](const struct GpioPinSpec& spec) {
        // Bit-bang pins carry no logical meaning
        struct GpioDescriptor* descriptor = gpio_descriptor_acquire(
            spec.gpio_controller, spec.pin, spec.flags | GPIO_FLAG_DIRECTION_OUTPUT, GPIO_OWNER_GPIO
        );
        if (descriptor == nullptr) {
            for (int i = 0; i < internal->descriptor_count; i++) {
                gpio_descriptor_release(internal->descriptors[i]);
            }
            return false;
        }
        internal->descriptors[internal->descriptor_count++] = descriptor;
        return true;
    };

    for (const struct GpioPinSpec* spec : data_specs) {
        if (!acquire(*spec)) {
            return ERROR_RESOURCE;
        }
    }
    for (const struct GpioPinSpec* spec : control_specs) {
        if (!acquire(*spec)) {
            return ERROR_RESOURCE;
        }
    }

    gpio_pin_t data_pins[8];
    for (int i = 0; i < 8; i++) {
        gpio_descriptor_get_pin_number(internal->descriptors[i], &data_pins[i]);
    }

    internal->cs_mask = bitbang_mask_for_pin(config->pin_cs.pin);
    internal->dc_mask = bitbang_mask_for_pin(config->pin_dc.pin);
    internal->wr_mask = bitbang_mask_for_pin(config->pin_wr.pin);

    gpio_set_level(static_cast<gpio_num_t>(config->pin_cs.pin), 1);
    gpio_set_level(static_cast<gpio_num_t>(config->pin_rd.pin), 1);
    gpio_set_level(static_cast<gpio_num_t>(config->pin_wr.pin), 1);
    gpio_set_level(static_cast<gpio_num_t>(config->pin_dc.pin), 1);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(static_cast<gpio_num_t>(data_pins[i]), 0);
    }

    // Precompute per-byte set/clear masks
    for (uint32_t value = 0; value < 256; value++) {
        St7789SoftI8080Internal::MaskPair set_pair = { 0, 0 };
        St7789SoftI8080Internal::MaskPair clear_pair = { 0, 0 };
        for (size_t i = 0; i < 8; i++) {
            St7789SoftI8080Internal::MaskPair pin_mask = bitbang_mask_for_pin(data_pins[i]);
            if (value & (1U << i)) {
                set_pair.low |= pin_mask.low;
                set_pair.high |= pin_mask.high;
            } else {
                clear_pair.low |= pin_mask.low;
                clear_pair.high |= pin_mask.high;
            }
        }
        internal->set_mask_by_byte[value] = set_pair;
        internal->clear_mask_by_byte[value] = clear_pair;
    }
    return ERROR_NONE;
}

static void bitbang_set_mask(const St7789SoftI8080Internal::MaskPair& mask) {
    GPIO.out_w1ts = mask.low;
    GPIO.out1_w1ts.val = mask.high;
}

static void bitbang_clear_mask(const St7789SoftI8080Internal::MaskPair& mask) {
    GPIO.out_w1tc = mask.low;
    GPIO.out1_w1tc.val = mask.high;
}

static void bitbang_write_byte(St7789SoftI8080Internal* internal, uint8_t value) {
    // Clear-then-set avoids read-modify-write on GPIO.out
    bitbang_clear_mask(internal->clear_mask_by_byte[value]);
    bitbang_set_mask(internal->set_mask_by_byte[value]);
    bitbang_clear_mask(internal->wr_mask);
    bitbang_set_mask(internal->wr_mask); // rising edge latches the byte
}

static void bitbang_send_cmd(St7789SoftI8080Internal* internal, uint8_t cmd, const uint8_t* params, size_t len) {
    bitbang_clear_mask(internal->cs_mask);
    bitbang_clear_mask(internal->dc_mask);
    bitbang_write_byte(internal, cmd);
    if (len > 0) {
        bitbang_set_mask(internal->dc_mask);
        for (size_t i = 0; i < len; i++) {
            bitbang_write_byte(internal, params[i]);
        }
    }
    bitbang_set_mask(internal->cs_mask);
}

static void bitbang_send_madctl(St7789SoftI8080Internal* internal) {
    bitbang_send_cmd(internal, 0x36, &internal->madctl, 1);
}

static error_t bitbang_init_panel(St7789SoftI8080Internal* internal, const St7789SoftI8080Config* config) {
    internal->gap_x = config->swap_xy ? config->gap_y : config->gap_x;
    internal->gap_y = config->swap_xy ? config->gap_x : config->gap_y;

    bitbang_send_cmd(internal, 0x01, nullptr, 0); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
    bitbang_send_cmd(internal, 0x11, nullptr, 0); // SLPOUT
    vTaskDelay(pdMS_TO_TICKS(120));

    internal->madctl = (config->bgr_order ? 0x08 : 0x00) |
                       (config->mirror_y ? 0x80 : 0x00) |
                       (config->mirror_x ? 0x40 : 0x00) |
                       (config->swap_xy ? 0x20 : 0x00);
    bitbang_send_madctl(internal);

    const uint8_t colmod = 0x55; // 16 bits per pixel
    bitbang_send_cmd(internal, 0x3A, &colmod, 1);
    // RAMCTRL little-endian
    const uint8_t ramctl[] = { 0x00, 0xF8 };
    bitbang_send_cmd(internal, 0xB0, ramctl, 2);

    if (config->invert_color) {
        bitbang_send_cmd(internal, 0x21, nullptr, 0); // INVON
    }
    bitbang_send_cmd(internal, 0x29, nullptr, 0); // DISPON
    vTaskDelay(pdMS_TO_TICKS(20));
    return ERROR_NONE;
}

static void bitbang_draw_bitmap(St7789SoftI8080Internal* internal, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const uint8_t* color_data) {
    int32_t x0 = x_start + internal->gap_x;
    int32_t x1 = x_end + internal->gap_x;
    int32_t y0 = y_start + internal->gap_y;
    int32_t y1 = y_end + internal->gap_y;

    const uint8_t caset[] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)((x1 - 1) >> 8), (uint8_t)(x1 - 1) };
    bitbang_send_cmd(internal, 0x2A, caset, 4);
    const uint8_t raset[] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)((y1 - 1) >> 8), (uint8_t)(y1 - 1) };
    bitbang_send_cmd(internal, 0x2B, raset, 4);

    bitbang_clear_mask(internal->cs_mask);
    bitbang_clear_mask(internal->dc_mask);
    bitbang_write_byte(internal, 0x2C); // RAMWR
    bitbang_set_mask(internal->dc_mask);
    size_t len = (size_t)(x1 - x0) * (y1 - y0) * 2;
    for (size_t i = 0; i < len; i++) {
        bitbang_write_byte(internal, color_data[i]);
    }
    bitbang_set_mask(internal->cs_mask);
}

static error_t bitbang_mirror(St7789SoftI8080Internal* internal, bool mirror_x, bool mirror_y) {
    internal->madctl = (internal->madctl & ~0xC0) | (mirror_x ? 0x40 : 0x00) | (mirror_y ? 0x80 : 0x00);
    bitbang_send_madctl(internal);
    return ERROR_NONE;
}

static error_t bitbang_swap_xy(St7789SoftI8080Internal* internal, bool swap_axes) {
    internal->madctl = (internal->madctl & ~0x20) | (swap_axes ? 0x20 : 0x00);
    bitbang_send_madctl(internal);
    return ERROR_NONE;
}

static error_t bitbang_invert_color(St7789SoftI8080Internal* internal, bool invert_color_data) {
    bitbang_send_cmd(internal, invert_color_data ? 0x21 : 0x20, nullptr, 0);
    return ERROR_NONE;
}

static error_t bitbang_disp_on_off(St7789SoftI8080Internal* internal, bool on_off) {
    bitbang_send_cmd(internal, on_off ? 0x29 : 0x28, nullptr, 0);
    return ERROR_NONE;
}

static error_t bitbang_disp_sleep(St7789SoftI8080Internal* internal, bool sleep) {
    bitbang_send_cmd(internal, sleep ? 0x10 : 0x11, nullptr, 0);
    if (!sleep) {
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    return ERROR_NONE;
}

// region Driver lifecycle

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<St7789SoftI8080Internal*>(malloc(sizeof(St7789SoftI8080Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    error_t err = bitbang_init_pins(internal, config);
    if (err != ERROR_NONE) {
        free(internal);
        return err;
    }
    err = bitbang_init_panel(internal, config);
    if (err != ERROR_NONE) {
        for (int i = 0; i < internal->descriptor_count; i++) {
            gpio_descriptor_release(internal->descriptors[i]);
        }
        free(internal);
        return err;
    }

    device_set_driver_data(device, internal);
    LOG_I(TAG, "Panel driven via bit-banged 8-bit GPIO transport");
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));

    for (int i = 0; i < internal->descriptor_count; i++) {
        gpio_descriptor_release(internal->descriptors[i]);
    }
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t st7789_soft_i8080_reset(Device*) {
    // The panel was fully initialised in start(); there is no reset line on the board this
    // targets, so re-init is a no-op.
    // TODO: Implement a reset sequence if a reset line is added for support for other boards.
    return ERROR_NONE;
}

static error_t st7789_soft_i8080_init(Device*) {
    return ERROR_NONE;
}

static error_t st7789_soft_i8080_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    bitbang_draw_bitmap(internal, x_start, y_start, x_end, y_end, static_cast<const uint8_t*>(color_data));
    return ERROR_NONE;
}

static error_t st7789_soft_i8080_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    return bitbang_mirror(internal, x_axis, y_axis);
}

static error_t st7789_soft_i8080_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    return bitbang_swap_xy(internal, swap_axes);
}

static bool st7789_soft_i8080_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool st7789_soft_i8080_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool st7789_soft_i8080_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

static int32_t st7789_soft_i8080_get_gap_x(Device* device) {
    const auto* config = GET_CONFIG(device);
    return config->swap_xy ? config->gap_y : config->gap_x;
}

static int32_t st7789_soft_i8080_get_gap_y(Device* device) {
    const auto* config = GET_CONFIG(device);
    return config->swap_xy ? config->gap_x : config->gap_y;
}

static error_t st7789_soft_i8080_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    internal->gap_x = x_gap;
    internal->gap_y = y_gap;
    return ERROR_NONE;
}

static error_t st7789_soft_i8080_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    return bitbang_invert_color(internal, invert_color_data);
}

static error_t st7789_soft_i8080_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    return bitbang_disp_on_off(internal, on_off);
}

static error_t st7789_soft_i8080_disp_sleep(Device* device, bool sleep) {
    auto* internal = static_cast<St7789SoftI8080Internal*>(device_get_driver_data(device));
    return bitbang_disp_sleep(internal, sleep);
}

static enum DisplayColorFormat st7789_soft_i8080_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565;
}

static uint16_t st7789_soft_i8080_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t st7789_soft_i8080_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void st7789_soft_i8080_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t st7789_soft_i8080_get_frame_buffer_count(Device*) {
    return 0;
}

static error_t st7789_soft_i8080_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi st7789_soft_i8080_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
        DISPLAY_CAPABILITY_CAP_SET_GAP | DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF |
        DISPLAY_CAPABILITY_SLEEP | DISPLAY_CAPABILITY_BACKLIGHT,
    .reset = st7789_soft_i8080_reset,
    .init = st7789_soft_i8080_init,
    .draw_bitmap = st7789_soft_i8080_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = st7789_soft_i8080_mirror,
    .swap_xy = st7789_soft_i8080_swap_xy,
    .get_swap_xy = st7789_soft_i8080_get_swap_xy,
    .get_mirror_x = st7789_soft_i8080_get_mirror_x,
    .get_mirror_y = st7789_soft_i8080_get_mirror_y,
    .set_gap = st7789_soft_i8080_set_gap,
    .get_gap_x = st7789_soft_i8080_get_gap_x,
    .get_gap_y = st7789_soft_i8080_get_gap_y,
    .invert_color = st7789_soft_i8080_invert_color,
    .disp_on_off = st7789_soft_i8080_disp_on_off,
    .disp_sleep = st7789_soft_i8080_disp_sleep,
    .get_color_format = st7789_soft_i8080_get_color_format,
    .get_resolution_x = st7789_soft_i8080_get_resolution_x,
    .get_resolution_y = st7789_soft_i8080_get_resolution_y,
    .get_frame_buffer = st7789_soft_i8080_get_frame_buffer,
    .get_frame_buffer_count = st7789_soft_i8080_get_frame_buffer_count,
    .get_backlight = st7789_soft_i8080_get_backlight,
    .has_capability = nullptr,
};

Driver st7789_soft_i8080_driver = {
    .name = "st7789_soft_i8080",
    .compatible = (const char*[]) { "sitronix,st7789-soft-i8080", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &st7789_soft_i8080_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &st7789_soft_i8080_module,
    .internal = nullptr
};