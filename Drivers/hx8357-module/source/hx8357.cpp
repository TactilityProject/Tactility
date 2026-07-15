// SPDX-License-Identifier: Apache-2.0
#include <drivers/hx8357.h>
#include <hx8357_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdlib>
#include <cstring>

#define TAG "HX8357"
#define GET_CONFIG(device) (static_cast<const Hx8357Config*>((device)->config))

namespace {

// HX8357-D command set (subset actually used). See
// https://cdn-shop.adafruit.com/datasheets/HX8357-D_DS_April2012.pdf
constexpr uint8_t HX8357_SWRESET = 0x01;
constexpr uint8_t HX8357_SLPOUT = 0x11;
constexpr uint8_t HX8357_INVOFF = 0x20;
constexpr uint8_t HX8357_INVON = 0x21;
constexpr uint8_t HX8357_DISPON = 0x29;
constexpr uint8_t HX8357_CASET = 0x2A;
constexpr uint8_t HX8357_PASET = 0x2B;
constexpr uint8_t HX8357_RAMWR = 0x2C;
constexpr uint8_t HX8357_MADCTL = 0x36;
constexpr uint8_t HX8357_COLMOD = 0x3A;
constexpr uint8_t HX8357_TEON = 0x35;
constexpr uint8_t HX8357_TEARLINE = 0x44;
constexpr uint8_t HX8357_SETOSC = 0xB0;
constexpr uint8_t HX8357_SETPWR1 = 0xB1;
constexpr uint8_t HX8357_SETRGB = 0xB3;
constexpr uint8_t HX8357D_SETCOM = 0xB6;
constexpr uint8_t HX8357D_SETCYC = 0xB4;
constexpr uint8_t HX8357D_SETC = 0xB9;
constexpr uint8_t HX8357D_SETSTBA = 0xC0;
constexpr uint8_t HX8357_SETPANEL = 0xCC;
constexpr uint8_t HX8357D_SETGAMMA = 0xE0;

// MADCTL bit indices, see the datasheet page 123.
constexpr uint8_t MADCTL_BIT_PAGE_COLUMN_ORDER = 5; // swap-xy
constexpr uint8_t MADCTL_BIT_COLUMN_ADDRESS_ORDER = 6; // mirror-x
constexpr uint8_t MADCTL_BIT_PAGE_ADDRESS_ORDER = 7; // mirror-y

// Bring-up command list, ported verbatim (byte-for-byte) from the deprecated HAL's hx8357.c
// initd[] table (Devices/unphone/Source/hx8357/hx8357.c) - the HX8357B variant (initb[]) is
// dead code there (displayType is hardcoded to HX8357D) and was not ported. Format matches the
// original: cmd, then a length byte (bit7 set means "this is actually a delay in 5ms units, no
// data follows"), then that many data bytes. A single 0 cmd byte ends the list.
constexpr uint8_t INIT_CMDS[] = {
    HX8357_SWRESET, 0x80 + 100 / 5, // Soft reset, then delay 100 ms
    HX8357D_SETC, 3,
        0xFF, 0x83, 0x57,
    0xFF, 0x80 + 500 / 5, // No command, just delay 500 ms
    HX8357_SETRGB, 4,
        0x80, 0x00, 0x06, 0x06, // 0x80 enables SDO pin (0x00 disables)
    HX8357D_SETCOM, 1,
        0x25, // -1.52V
    HX8357_SETOSC, 1,
        0x68, // Normal mode 70Hz, Idle mode 55 Hz
    HX8357_SETPANEL, 1,
        0x05, // BGR, Gate direction swapped
    HX8357_SETPWR1, 6,
        0x00, // Not deep standby
        0x15, // BT
        0x1C, // VSPR
        0x1C, // VSNR
        0x83, // AP
        0xAA, // FS
    HX8357D_SETSTBA, 6,
        0x50, // OPON normal
        0x50, // OPON idle
        0x01, // STBA
        0x3C, // STBA
        0x1E, // STBA
        0x08, // GEN
    HX8357D_SETCYC, 7,
        0x02, // NW 0x02
        0x40, // RTN
        0x00, // DIV
        0x2A, // DUM
        0x2A, // DUM
        0x0D, // GDON
        0x78, // GDOFF
    HX8357D_SETGAMMA, 34,
        0x02, 0x0A, 0x11, 0x1d, 0x23, 0x35, 0x41, 0x4b, 0x4b,
        0x42, 0x3A, 0x27, 0x1B, 0x08, 0x09, 0x03, 0x02, 0x0A,
        0x11, 0x1d, 0x23, 0x35, 0x41, 0x4b, 0x4b, 0x42, 0x3A,
        0x27, 0x1B, 0x08, 0x09, 0x03, 0x00, 0x01,
    HX8357_COLMOD, 1,
        0x57, // 24bpp
    HX8357_MADCTL, 1,
        0xC0,
    HX8357_TEON, 1,
        0x00, // TW off
    HX8357_TEARLINE, 2,
        0x00, 0x02,
    HX8357_SLPOUT, 0x80 + 150 / 5, // Exit sleep, then delay 150 ms
    HX8357_DISPON, 0x80 + 50 / 5, // Main screen turn on, delay 50 ms
    0, // END OF COMMAND LIST
};

} // namespace

struct Hx8357Internal {
    spi_device_handle_t spi_handle;
    gpio_num_t dc_pin;
    size_t max_transfer_size;
    // Live MADCTL orientation bits, seeded from config at start() and updated in place by
    // mirror()/swap_xy() - NOT reconstructed from the static config each call. swap_xy() and
    // mirror() are invoked back-to-back by lvgl_display_apply_rotation() on every rotation
    // change, so rebuilding from config would silently discard whichever bit the other call
    // just set.
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
};

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// region Bring-up protocol

static void spi_send(spi_device_handle_t spi, const uint8_t* data, size_t length) {
    if (length == 0) {
        return;
    }
    spi_transaction_t transaction = {};
    transaction.length = length * 8;
    if (length <= 4) {
        transaction.flags = SPI_TRANS_USE_TXDATA;
        memcpy(transaction.tx_data, data, length);
    } else {
        transaction.tx_buffer = data;
    }
    // Blocking: physically completes before returning, unlike esp_lcd_panel_draw_bitmap() -
    // no semaphore/ISR-callback dance needed to honor DisplayApi's synchronous draw_bitmap contract.
    spi_device_transmit(spi, &transaction);
}

static void send_cmd(const Hx8357Internal* internal, uint8_t cmd) {
    gpio_set_level(internal->dc_pin, 0);
    spi_send(internal->spi_handle, &cmd, 1);
}

// Chunked to stay under the SPI bus's max single-transaction transfer size (e.g. RAMWR pixel
// bursts can be hundreds of KB). CS toggles between chunks, which the panel tolerates: it only
// resets its RAMWR auto-increment pointer on a new command byte, not on CS deselect.
static void send_data(const Hx8357Internal* internal, const uint8_t* data, size_t length) {
    gpio_set_level(internal->dc_pin, 1);
    while (length > 0) {
        size_t chunk = length < internal->max_transfer_size ? length : internal->max_transfer_size;
        spi_send(internal->spi_handle, data, chunk);
        data += chunk;
        length -= chunk;
    }
}

static void run_init_cmds(const Hx8357Internal* internal) {
    const uint8_t* addr = INIT_CMDS;
    uint8_t cmd;
    while ((cmd = *addr++) > 0) { // 0 command ends the list
        uint8_t x = *addr++;
        uint8_t num_args = x & 0x7F;
        if (cmd != 0xFF) { // 0xFF is a no-op placeholder (only used to carry a delay)
            send_cmd(internal, cmd);
            if (!(x & 0x80)) {
                send_data(internal, addr, num_args);
                addr += num_args;
            }
        }
        if (x & 0x80) { // high bit set: num_args is actually a delay, in 5ms units
            vTaskDelay(pdMS_TO_TICKS(num_args * 5));
        }
    }
}

static void send_madctl(const Hx8357Internal* internal) {
    uint8_t madctl = 0;
    if (internal->swap_xy) madctl |= (1 << MADCTL_BIT_PAGE_COLUMN_ORDER);
    if (internal->mirror_x) madctl |= (1 << MADCTL_BIT_COLUMN_ADDRESS_ORDER);
    if (internal->mirror_y) madctl |= (1 << MADCTL_BIT_PAGE_ADDRESS_ORDER);
    send_cmd(internal, HX8357_MADCTL);
    send_data(internal, &madctl, 1);
}

// endregion

// region Driver lifecycle

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    GpioPinSpec cs_pin;
    if (esp32_spi_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Hx8357Internal*>(malloc(sizeof(Hx8357Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    internal->dc_pin = static_cast<gpio_num_t>(pin_or_unused(config->pin_dc));
    // Clamped below the bus's configured max_transfer_size: that value only bounds the DMA
    // buffer/descriptor allocation, not the SPI peripheral's own per-transaction bit-length
    // register (e.g. 18 bits = 32768 bytes on ESP32-S3, 24 bits elsewhere) - so a large
    // max-transfer-size in the devicetree (sized for e.g. an SD card) can still overflow a
    // single spi_device_transmit() here. 4096 is safely under that hardware limit on every
    // ESP32 variant.
    constexpr size_t MAX_SPI_CHUNK_SIZE = 4096;
    internal->max_transfer_size = (spi_config->max_transfer_size > 0 && static_cast<size_t>(spi_config->max_transfer_size) < MAX_SPI_CHUNK_SIZE)
        ? static_cast<size_t>(spi_config->max_transfer_size)
        : MAX_SPI_CHUNK_SIZE;
    gpio_config_t dc_config = {
        .pin_bit_mask = 1ULL << internal->dc_pin,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&dc_config) != ESP_OK) {
        LOG_E(TAG, "Failed to configure DC pin");
        free(internal);
        return ERROR_RESOURCE;
    }

    spi_device_interface_config_t device_config = {
        .mode = 0,
        .clock_speed_hz = static_cast<int>(config->pixel_clock_hz),
        .spics_io_num = pin_or_unused(cs_pin),
        .flags = 0,
        .queue_size = 1,
    };

    if (spi_bus_add_device(spi_config->host, &device_config, &internal->spi_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to add SPI device");
        free(internal);
        return ERROR_RESOURCE;
    }

    // Hardware reset, in addition to the SWRESET the bring-up command list sends
    int reset_pin = pin_or_unused(config->pin_reset);
    if (reset_pin != -1) {
        gpio_config_t reset_config = {
            .pin_bit_mask = 1ULL << reset_pin,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        if (gpio_config(&reset_config) != ESP_OK) {
            LOG_E(TAG, "Failed to configure reset pin");
            spi_bus_remove_device(internal->spi_handle);
            free(internal);
            return ERROR_RESOURCE;
        }
        gpio_set_level(static_cast<gpio_num_t>(reset_pin), config->reset_active_high ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(static_cast<gpio_num_t>(reset_pin), config->reset_active_high ? 0 : 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;

    run_init_cmds(internal);
    send_madctl(internal);
    send_cmd(internal, config->invert_color ? HX8357_INVON : HX8357_INVOFF);

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));

    if (spi_bus_remove_device(internal->spi_handle) != ESP_OK) {
        LOG_E(TAG, "Failed to remove SPI device");
        free(internal);
        return ERROR_RESOURCE;
    }

    free(internal);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t hx8357_reset(Device*) {
    // No standalone reset beyond what start_device() already does; matches the deprecated HAL
    // (its reset() DisplayDevice override was never wired to anything beyond initial bring-up).
    return ERROR_NONE;
}

static error_t hx8357_init(Device*) {
    return ERROR_NONE;
}

static error_t hx8357_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    // x_end/y_end are exclusive (see lvgl_display.c); CASET/PASET want an inclusive last pixel.
    const int32_t x1 = x_start + config->gap_x;
    const int32_t x2 = x_end + config->gap_x - 1;
    const int32_t y1 = y_start + config->gap_y;
    const int32_t y2 = y_end + config->gap_y - 1;

    const uint8_t xb[4] = {
        static_cast<uint8_t>((x1 >> 8) & 0xFF), static_cast<uint8_t>(x1 & 0xFF),
        static_cast<uint8_t>((x2 >> 8) & 0xFF), static_cast<uint8_t>(x2 & 0xFF),
    };
    const uint8_t yb[4] = {
        static_cast<uint8_t>((y1 >> 8) & 0xFF), static_cast<uint8_t>(y1 & 0xFF),
        static_cast<uint8_t>((y2 >> 8) & 0xFF), static_cast<uint8_t>(y2 & 0xFF),
    };

    send_cmd(internal, HX8357_CASET);
    send_data(internal, xb, 4);
    send_cmd(internal, HX8357_PASET);
    send_data(internal, yb, 4);
    send_cmd(internal, HX8357_RAMWR);

    const size_t pixel_count = static_cast<size_t>(x_end - x_start) * static_cast<size_t>(y_end - y_start);
    send_data(internal, static_cast<const uint8_t*>(color_data), pixel_count * 3); // RGB888 = 3 bytes/pixel

    return ERROR_NONE;
}

static error_t hx8357_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));
    internal->mirror_x = x_axis;
    internal->mirror_y = y_axis;
    send_madctl(internal);
    return ERROR_NONE;
}

static error_t hx8357_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));
    internal->swap_xy = swap_axes;
    send_madctl(internal);
    return ERROR_NONE;
}

// Reads the devicetree-configured baseline, not live hardware state - same convention as
// ili9341-module/st7789-module: mirror()/swap_xy() calls after start_device() don't change
// what "rotation 0" means here.
static bool hx8357_get_swap_xy(Device* device) {
    return GET_CONFIG(device)->swap_xy;
}

static bool hx8357_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool hx8357_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

// set_gap is not exposed: this controller's fixed CASET/PASET-per-draw protocol only supports the
// static gap-x/gap-y devicetree config already folded into draw_bitmap(); matches the deprecated
// HAL, which never exposed a runtime gap either.

static error_t hx8357_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));
    send_cmd(internal, invert_color_data ? HX8357_INVON : HX8357_INVOFF);
    return ERROR_NONE;
}

static error_t hx8357_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));
    send_cmd(internal, on_off ? HX8357_DISPON : 0x28 /* HX8357_DISPOFF */);
    return ERROR_NONE;
}

static error_t hx8357_disp_sleep(Device* device, bool sleep) {
    auto* internal = static_cast<Hx8357Internal*>(device_get_driver_data(device));
    send_cmd(internal, sleep ? 0x10 /* HX8357_SLPIN */ : HX8357_SLPOUT);
    return ERROR_NONE;
}

static enum DisplayColorFormat hx8357_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB888;
}

static uint16_t hx8357_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t hx8357_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void hx8357_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t hx8357_get_frame_buffer_count(Device*) {
    return 0;
}

static error_t hx8357_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi hx8357_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
        DISPLAY_CAPABILITY_INVERT_COLOR | DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLEEP |
        DISPLAY_CAPABILITY_BACKLIGHT,
    .reset = hx8357_reset,
    .init = hx8357_init,
    .draw_bitmap = hx8357_draw_bitmap,
    .mirror = hx8357_mirror,
    .swap_xy = hx8357_swap_xy,
    .get_swap_xy = hx8357_get_swap_xy,
    .get_mirror_x = hx8357_get_mirror_x,
    .get_mirror_y = hx8357_get_mirror_y,
    .set_gap = nullptr,
    .invert_color = hx8357_invert_color,
    .disp_on_off = hx8357_disp_on_off,
    .disp_sleep = hx8357_disp_sleep,
    .get_color_format = hx8357_get_color_format,
    .get_resolution_x = hx8357_get_resolution_x,
    .get_resolution_y = hx8357_get_resolution_y,
    .get_frame_buffer = hx8357_get_frame_buffer,
    .get_frame_buffer_count = hx8357_get_frame_buffer_count,
    .get_backlight = hx8357_get_backlight,
    .has_capability = nullptr,
};

Driver hx8357_driver = {
    .name = "hx8357",
    .compatible = (const char*[]) { "himax,hx8357", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &hx8357_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &hx8357_module,
    .internal = nullptr
};
