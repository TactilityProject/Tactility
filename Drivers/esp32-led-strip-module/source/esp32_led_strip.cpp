// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <esp32_led_strip_module.h>
#include <tactility/drivers/led_strip.h>
#include <tactility/drivers/gpio.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <drivers/esp32_led_strip.h>
#include <cstdlib>
#include <esp32_led_strip_rmt.h>

#define TAG "Esp32LedStrip"

#define GET_CONFIG(device) (static_cast<const LedStripConfig*>((device)->config))
#define GET_INTERNAL(device) (static_cast<LedStripInternal*>(device_get_driver_data(device)))

struct LedStripInternal {
    led_strip_handle_t strip;
    GpioPinSpec data_pin;
    uint16_t led_count;
    uint8_t global_brightness;
};

// Gamma correction lookup table for 8-bit brightness/color scaling
static const uint8_t gamma8[] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   3,
      3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,   7,
      7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,  12,  13,
     13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,  20,  20,
     21,  21,  22,  22,  23,  24,  24,  25,  25,  26,  27,  27,  28,  29,  29,  30,
     31,  31,  32,  33,  34,  34,  35,  36,  37,  37,  38,  39,  40,  41,  42,  42,
     43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,
     59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  74,  75,
     76,  77,  78,  79,  81,  82,  83,  84,  86,  87,  88,  90,  91,  92,  94,  95,
     96,  98,  99, 101, 102, 104, 105, 107, 108, 110, 111, 113, 114, 116, 117, 119,
    121, 122, 124, 125, 127, 129, 130, 132, 134, 135, 137, 139, 141, 142, 144, 146,
    148, 150, 151, 153, 155, 157, 159, 161, 163, 165, 166, 168, 170, 172, 174, 176,
    178, 180, 182, 184, 186, 188, 191, 193, 195, 197, 199, 201, 204, 206, 208, 210,
    213, 215, 217, 220, 222, 224, 227, 229, 231, 234, 236, 239, 241, 244, 246, 249,
    251, 254, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

static error_t create_strip(const LedStripConfig* source, led_strip_handle_t* out_strip) {
    led_strip_config_t strip_config_internal = {
        .strip_gpio_num = static_cast<gpio_num_t>(source->pin_data.pin),
        .max_leds = source->led_count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 0,
        .flags = {
            .with_dma = 0,
        }
    };

    led_strip_handle_t strip = nullptr;
    esp_err_t err = led_strip_new_rmt_device(&strip_config_internal, &rmt_config, &strip);
    if (err != ESP_OK) {
        return ERROR_RESOURCE;
    }

    *out_strip = strip;
    return ERROR_NONE;
}

static error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = static_cast<LedStripInternal*>(std::calloc(1, sizeof(LedStripInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->data_pin = static_cast<GpioPinSpec>(config->pin_data);
    internal->led_count = config->led_count;
    internal->global_brightness = 255;

    led_strip_handle_t strip = nullptr;
    if (create_strip(config, &strip) != ERROR_NONE) {
        std::free(internal);
        return ERROR_RESOURCE;
    }

    internal->strip = strip;

    device_set_driver_data(device, internal);

    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = GET_INTERNAL(device);

    led_strip_rmt_del(internal->strip);

    std::free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

static error_t apply_single(Device* device, size_t index, const LedRgb& color, uint8_t brightness) {
    auto* internal = GET_INTERNAL(device);

    if (index >= internal->led_count) return ERROR_INVALID_ARGUMENT;

    uint32_t sr = (color.r * brightness) / 255;
    uint32_t sg = (color.g * brightness) / 255;
    uint32_t sb = (color.b * brightness) / 255;

    esp_err_t err = led_strip_rmt_set_pixel(internal->strip, index, gamma8[sr], gamma8[sg], gamma8[sb]);
    if (err != ESP_OK) {
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

static error_t apply_range(Device* device, size_t first, size_t count, const LedRgb* colors, uint8_t brightness) {
    auto* internal = GET_INTERNAL(device);

    if (first > internal->led_count) return ERROR_INVALID_ARGUMENT;
    if (count > internal->led_count - first) return ERROR_INVALID_ARGUMENT;
    if (colors == nullptr) return ERROR_INVALID_ARGUMENT;

    for (size_t i = 0; i < count; i++) {
        uint32_t sr = (colors[i].r * brightness) / 255;
        uint32_t sg = (colors[i].g * brightness) / 255;
        uint32_t sb = (colors[i].b * brightness) / 255;

        esp_err_t err = led_strip_rmt_set_pixel(internal->strip, first + i, gamma8[sr], gamma8[sg], gamma8[sb]);
        if (err != ESP_OK) {
            return ERROR_RESOURCE;
        }
    }
    return ERROR_NONE;
}

static error_t apply_fill(Device* device, size_t first, size_t count, const LedRgb& color, uint8_t brightness)
{
    auto* internal = GET_INTERNAL(device);
    if (first > internal->led_count) return ERROR_INVALID_ARGUMENT;
    if (count > internal->led_count - first) return ERROR_INVALID_ARGUMENT;

    for (size_t i = 0; i < count; i++) {
        uint32_t sr = (color.r * brightness) / 255;
        uint32_t sg = (color.g * brightness) / 255;
        uint32_t sb = (color.b * brightness) / 255;

        esp_err_t err = led_strip_rmt_set_pixel(internal->strip, first + i, gamma8[sr], gamma8[sg], gamma8[sb]);
        if (err != ESP_OK) {
            return ERROR_RESOURCE;
        }
    }
    return ERROR_NONE;
}

extern "C" {

error_t esp32_led_strip_set_single_led_color(Device* device, size_t index, const LedRgb color) {
    auto* internal = GET_INTERNAL(device);
    return apply_single(device, index, color, internal->global_brightness);
}

error_t esp32_led_strip_set_single_led_color_and_brightness(Device* device, size_t index, const LedRgb color, uint8_t brightness) {
    return apply_single(device, index, color, brightness);
}

error_t esp32_led_strip_set_led_range_color(Device* device, size_t first, size_t count, const LedRgb* color) {
    auto* internal = GET_INTERNAL(device);
    return apply_range(device, first, count, color, internal->global_brightness);
}

error_t esp32_led_strip_set_led_range_color_and_brightness(Device* device, size_t first, size_t count, const LedRgb* color, uint8_t brightness) {
    return apply_range(device, first, count, color, brightness);
}

error_t esp32_led_strip_fill_led_range_color(Device* device, size_t first, size_t count, const LedRgb color) {
    auto* internal = GET_INTERNAL(device);
    return apply_fill(device, first, count, color, internal->global_brightness);
}

error_t esp32_led_strip_fill_led_range_color_and_brightness(Device* device, size_t first, size_t count, const LedRgb color, uint8_t brightness) {
    return apply_fill(device, first, count, color, brightness);
}

error_t esp32_led_strip_set_brightness(Device* device, uint8_t brightness) {
    auto* internal = GET_INTERNAL(device);
    internal->global_brightness = brightness;
    return ERROR_NONE;
}

error_t esp32_led_strip_show(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal->strip != nullptr) {
        esp_err_t err = led_strip_rmt_refresh(internal->strip);
        if (err != ESP_OK) {
            return ERROR_RESOURCE;
        }
    }
    return ERROR_NONE;
}

error_t esp32_led_strip_clear(Device* device) {
    auto* internal = GET_INTERNAL(device);
    if (internal->strip != nullptr) {
        esp_err_t err = led_strip_rmt_clear(internal->strip);
        if (err != ESP_OK) {
            return ERROR_RESOURCE;
        }
    }
    return ERROR_NONE;
}

error_t esp32_led_strip_get_length(Device* device, uint16_t* out_length) {
    auto* internal = GET_INTERNAL(device);
    *out_length = internal->led_count;
    return ERROR_NONE;
}

static constexpr LedStripApi LED_STRIP_API = {
    .set_single_led_color = esp32_led_strip_set_single_led_color,
    .set_single_led_color_and_brightness = esp32_led_strip_set_single_led_color_and_brightness,
    .set_led_range_color = esp32_led_strip_set_led_range_color,
    .set_led_range_color_and_brightness = esp32_led_strip_set_led_range_color_and_brightness,
    .fill_led_range_color = esp32_led_strip_fill_led_range_color,
    .fill_led_range_color_and_brightness = esp32_led_strip_fill_led_range_color_and_brightness,
    .set_brightness = esp32_led_strip_set_brightness,
    .show = esp32_led_strip_show,
    .clear = esp32_led_strip_clear,
    .get_length = esp32_led_strip_get_length,
};

extern Module esp32_led_strip_module;

Driver esp32_led_strip_driver = {
    .name = "esp32_led_strip",
    .compatible = (const char*[]) { "espressif,esp32-led-strip", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &LED_STRIP_API,
    .device_type = &LED_STRIP_TYPE,
    .owner = &esp32_led_strip_module,
    .internal = nullptr,
};

} // extern "C"
