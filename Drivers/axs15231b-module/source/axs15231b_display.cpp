// SPDX-License-Identifier: Apache-2.0
#include <drivers/axs15231b_display.h>
#include <axs15231b_module.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>

#include <esp_err.h>
#include <esp_lcd_axs15231b.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <driver/gpio.h>
#include <freertos/semphr.h>

#include <cstdlib>

#define TAG "AXS15231B"
#define GET_CONFIG(device) (static_cast<const Axs15231bDisplayConfig*>((device)->config))

struct Axs15231bDisplayInternal {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    // Given from ISR context by on_color_trans_done() once a queued transfer physically
    // completes. draw_bitmap() blocks on this so it can honor DisplayApi's synchronous contract
    // (see lvgl_display.c: the caller reuses/overwrites the color buffer as soon as draw_bitmap
    // returns) - esp_lcd_panel_draw_bitmap() itself only queues the transfer and returns early.
    SemaphoreHandle_t draw_done_semaphore;
    // Non-null only when a TE (Tearing-Effect) pin is configured. Signaled by te_isr_handler()
    // on each rising edge, so draw_bitmap() can wait for the next V-blank before transferring.
    SemaphoreHandle_t te_semaphore;
    bool te_isr_installed;
    // Whether we're the one who called gpio_install_isr_service() - if so, we must be the one to
    // uninstall it, but only if no other pin on the system is still relying on it.
    bool te_isr_service_installed_by_us;
    axs15231b_lcd_init_cmd_t* parsed_init_cmds;
};

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->draw_done_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static void IRAM_ATTR te_isr_handler(void* arg) {
    auto* semaphore = static_cast<SemaphoreHandle_t>(arg);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(semaphore, &high_task_woken);
    if (high_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

static gpio_num_t pin_or_nc(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? GPIO_NUM_NC : static_cast<gpio_num_t>(pin.pin);
}

// Unpacks the devicetree's flat [cmd, data_len, delay_ms, data_len bytes...] encoding (produced
// by the devicetree compiler's "array" property type - see init-sequence in
// bindings/axs,axs15231b.yaml) into a heap-allocated axs15231b_lcd_init_cmd_t array.
static bool parse_init_sequence(const uint8_t* bytes, uint32_t length, axs15231b_lcd_init_cmd_t** out_cmds, uint16_t* out_count) {
    uint32_t count = 0;
    for (uint32_t offset = 0; offset < length; count++) {
        if (offset + 3 > length) {
            LOG_E(TAG, "init-sequence truncated: entry header runs past the end of the array");
            return false;
        }
        offset += 3 + bytes[offset + 1];
        if (offset > length) {
            LOG_E(TAG, "init-sequence truncated: entry data runs past the end of the array");
            return false;
        }
    }

    auto* cmds = static_cast<axs15231b_lcd_init_cmd_t*>(malloc(count * sizeof(axs15231b_lcd_init_cmd_t)));
    if (cmds == nullptr) {
        return false;
    }

    uint32_t offset = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t data_len = bytes[offset + 1];
        cmds[i] = {
            .cmd = bytes[offset],
            .data = data_len > 0 ? &bytes[offset + 3] : nullptr,
            .data_bytes = data_len,
            .delay_ms = bytes[offset + 2],
        };
        offset += 3 + data_len;
    }

    *out_cmds = cmds;
    *out_count = (uint16_t)count;
    return true;
}

// region Driver lifecycle

// Best-effort: a TE pin is a hardware refinement, not a requirement, so failures here are logged
// and left for the caller to treat as non-fatal (matches the original deprecated-HAL driver,
// which continued without TE sync if setup failed).
static bool setup_te_sync(Axs15231bDisplayInternal* internal, gpio_num_t te_pin) {
    if (te_pin == GPIO_NUM_NC) {
        return true;
    }

    internal->te_semaphore = xSemaphoreCreateBinary();
    if (internal->te_semaphore == nullptr) {
        LOG_E(TAG, "Failed to create TE sync semaphore");
        return false;
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << te_pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    if (gpio_config(&io_conf) != ESP_OK) {
        LOG_E(TAG, "Failed to configure TE GPIO");
        vSemaphoreDelete(internal->te_semaphore);
        internal->te_semaphore = nullptr;
        return false;
    }

    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret == ESP_OK) {
        internal->te_isr_service_installed_by_us = true;
    } else if (ret != ESP_ERR_INVALID_STATE) { // ESP_ERR_INVALID_STATE means it's already installed elsewhere
        LOG_E(TAG, "Failed to install GPIO ISR service");
        vSemaphoreDelete(internal->te_semaphore);
        internal->te_semaphore = nullptr;
        return false;
    }

    if (gpio_isr_handler_add(te_pin, te_isr_handler, internal->te_semaphore) != ESP_OK) {
        LOG_E(TAG, "Failed to add TE ISR handler");
        if (internal->te_isr_service_installed_by_us) {
            gpio_uninstall_isr_service();
            internal->te_isr_service_installed_by_us = false;
        }
        vSemaphoreDelete(internal->te_semaphore);
        internal->te_semaphore = nullptr;
        return false;
    }

    internal->te_isr_installed = true;
    return true;
}

static void teardown_te_sync(Axs15231bDisplayInternal* internal, gpio_num_t te_pin) {
    if (internal->te_isr_installed) {
        gpio_isr_handler_remove(te_pin);
        gpio_intr_disable(te_pin);
        internal->te_isr_installed = false;
    }
    if (internal->te_isr_service_installed_by_us) {
        gpio_uninstall_isr_service();
        internal->te_isr_service_installed_by_us = false;
    }
    if (internal->te_semaphore != nullptr) {
        vSemaphoreDelete(internal->te_semaphore);
        internal->te_semaphore = nullptr;
    }
}

static error_t start(Device* device) {
    auto* parent = device_get_parent(device);
    check(device_get_type(parent) == &SPI_CONTROLLER_TYPE);

    const auto* spi_config = static_cast<const Esp32SpiConfig*>(parent->config);
    const auto* config = GET_CONFIG(device);

    struct GpioPinSpec cs_pin;
    if (esp32_spi_get_cs_pin(device, &cs_pin) != ERROR_NONE) {
        LOG_E(TAG, "Failed to resolve CS pin");
        return ERROR_RESOURCE;
    }

    auto* internal = static_cast<Axs15231bDisplayInternal*>(malloc(sizeof(Axs15231bDisplayInternal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->te_semaphore = nullptr;
    internal->te_isr_installed = false;
    internal->te_isr_service_installed_by_us = false;
    internal->parsed_init_cmds = nullptr;

    const axs15231b_lcd_init_cmd_t* init_cmds = nullptr;
    uint16_t init_cmds_size = 0;
    if (config->init_sequence != nullptr && config->init_sequence_length > 0) {
        if (!parse_init_sequence(config->init_sequence, config->init_sequence_length, &internal->parsed_init_cmds, &init_cmds_size)) {
            LOG_E(TAG, "Failed to parse init-sequence property");
            free(internal);
            return ERROR_INVALID_ARGUMENT;
        }
        init_cmds = internal->parsed_init_cmds;
    }

    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->draw_done_semaphore == nullptr) {
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    // AXS15231B is only ever driven over QSPI in this codebase (no plain-SPI/i80 board has shown
    // up yet), so quad_mode/lcd_cmd_bits/lcd_param_bits are fixed rather than devicetree knobs -
    // matches AXS15231B_PANEL_IO_QSPI_CONFIG() in esp_lcd_axs15231b.h. dc_gpio_num is unused in
    // QSPI mode (command/data framing goes over the command byte instead of a DC line).
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = pin_or_unused(cs_pin),
        .dc_gpio_num = -1,
        .spi_mode = 3,
        .pclk_hz = config->pixel_clock_hz,
        .trans_queue_depth = config->transaction_queue_depth,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx = internal,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0,
        .flags = {
            .dc_high_on_cmd = 0,
            .dc_low_on_data = 0,
            .dc_low_on_param = 0,
            .octal_mode = 0,
            .quad_mode = 1,
            .sio_mode = 0,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_config->host, &io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    axs15231b_vendor_config_t vendor_config = {
        .init_cmds = init_cmds,
        .init_cmds_size = init_cmds_size,
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = pin_or_unused(config->pin_reset),
        .rgb_ele_order = config->bgr_order ? LCD_RGB_ELEMENT_ORDER_BGR : LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = { .reset_active_high = config->reset_active_high },
        .vendor_config = &vendor_config,
    };

    ret = esp_lcd_new_panel_axs15231b(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Bring-up sequence. swap_xy is intentionally not called (and not exposed in DisplayApi below):
    // It doesn't work on this chip/panel combination.
    //
    // esp_lcd_axs15231b's disp_on_off callback is wired up backwards: its body branches on a
    // parameter it names "off" (true -> DISPOFF, false -> DISPON), but esp_lcd_panel_disp_on_off()
    // forwards its "on_off" argument straight through with no inversion - so passing true here
    // actually switches the panel OFF. Pass false to really turn it on (confirmed against the
    // deleted deprecated-HAL driver, which called this same function with false for the same
    // reason). See axs15231b_disp_on_off() below, which un-inverts this for DisplayApi callers.
    //
    // (note: all of this was tested on guition-jc3248w535c only)
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK &&
        esp_lcd_panel_invert_color(internal->panel_handle, config->invert_color) == ESP_OK &&
        esp_lcd_panel_disp_on_off(internal->panel_handle, false) == ESP_OK;

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal->parsed_init_cmds);
        free(internal);
        return ERROR_RESOURCE;
    }

    if (!setup_te_sync(internal, pin_or_nc(config->pin_te))) {
        LOG_W(TAG, "TE sync setup failed, continuing without TE synchronization");
    }

    device_set_driver_data(device, internal);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    const auto* config = GET_CONFIG(device);
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));

    teardown_te_sync(internal, pin_or_nc(config->pin_te));

    if (internal->panel_handle != nullptr) {
        if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel");
            return ERROR_RESOURCE;
        }
        internal->panel_handle = nullptr;
    }

    if (internal->io_handle != nullptr) {
        if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel IO");
            return ERROR_RESOURCE;
        }
        internal->io_handle = nullptr;
    }

    vSemaphoreDelete(internal->draw_done_semaphore);
    free(internal->parsed_init_cmds);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t axs15231b_reset(Device* device) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t axs15231b_init(Device* device) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t axs15231b_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));

    // Best-effort wait for the next V-blank pulse before transferring, to reduce visible tearing.
    // Non-fatal if it times out (matches the original deprecated-HAL driver) - draw_bitmap()
    // still has to happen even if TE sync missed its window.
    if (internal->te_semaphore != nullptr) {
        xSemaphoreTake(internal->te_semaphore, 0); // drain any already-pending signal
        xSemaphoreTake(internal->te_semaphore, pdMS_TO_TICKS(20));
    }

    // Drain any stale signal left over from a prior non-draw transaction (bring-up commands like
    // reset/init also complete through on_color_trans_done), so the take() below can only be
    // satisfied by this draw's own transfer completing.
    xSemaphoreTake(internal->draw_done_semaphore, 0);

    if (esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, color_data) != ESP_OK) {
        return ERROR_RESOURCE;
    }

    xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    return ERROR_NONE;
}

static error_t axs15231b_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static bool axs15231b_get_mirror_x(Device* device) {
    return GET_CONFIG(device)->mirror_x;
}

static bool axs15231b_get_mirror_y(Device* device) {
    return GET_CONFIG(device)->mirror_y;
}

// swap_xy/set_gap/disp_sleep are not exposed: swap_xy is confirmed non-functional on this
// chip/panel combination on real hardware (see start()'s comment), and the AXS15231B component
// doesn't implement set_gap or disp_sleep at all.

static error_t axs15231b_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));
    return esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t axs15231b_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Axs15231bDisplayInternal*>(device_get_driver_data(device));
    // Inverted: see the comment on the disp_on_off call in start() above.
    return esp_lcd_panel_disp_on_off(internal->panel_handle, !on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
}

// _SWAPPED (not plain RGB565): the panel expects each 16-bit pixel high-byte-first over the QSPI
// bus, but this CPU is little-endian - the original deprecated-HAL driver did the equivalent
// byte-swap by hand in its custom flush callback (lv_draw_sw_rgb565_swap()); the generic
// lvgl-module bridge does it for us once we report this format (see lvgl_display_map_color_format()).
static enum DisplayColorFormat axs15231b_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565_SWAPPED;
}

static uint16_t axs15231b_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t axs15231b_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

static void axs15231b_get_frame_buffer(Device*, uint8_t, void** out_buffer) {
    *out_buffer = nullptr;
}

static uint8_t axs15231b_get_frame_buffer_count(Device*) {
    return 0;
}

static error_t axs15231b_get_backlight(Device* device, Device** backlight) {
    auto* configured_backlight = GET_CONFIG(device)->backlight;
    if (configured_backlight == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = configured_backlight;
    return ERROR_NONE;
}

// endregion

static const DisplayApi axs15231b_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_INVERT_COLOR |
        DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_BACKLIGHT,
    .reset = axs15231b_reset,
    .init = axs15231b_init,
    .draw_bitmap = axs15231b_draw_bitmap,
    .mirror = axs15231b_mirror,
    .swap_xy = nullptr,
    .get_swap_xy = nullptr,
    .get_mirror_x = axs15231b_get_mirror_x,
    .get_mirror_y = axs15231b_get_mirror_y,
    .set_gap = nullptr,
    .invert_color = axs15231b_invert_color,
    .disp_on_off = axs15231b_disp_on_off,
    .disp_sleep = nullptr,
    .get_color_format = axs15231b_get_color_format,
    .get_resolution_x = axs15231b_get_resolution_x,
    .get_resolution_y = axs15231b_get_resolution_y,
    .get_frame_buffer = axs15231b_get_frame_buffer,
    .get_frame_buffer_count = axs15231b_get_frame_buffer_count,
    .get_backlight = axs15231b_get_backlight,
    .has_capability = nullptr,
};

Driver axs15231b_display_driver = {
    .name = "axs15231b_display",
    .compatible = (const char*[]) { "axs,axs15231b", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = &axs15231b_display_api,
    .device_type = &DISPLAY_TYPE,
    .owner = &axs15231b_module,
    .internal = nullptr
};
