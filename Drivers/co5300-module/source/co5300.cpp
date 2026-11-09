// SPDX-License-Identifier: Apache-2.0
#include <drivers/co5300.h>

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/backlight.h>
#include <tactility/drivers/display.h>
#include <tactility/drivers/esp32_spi.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/spi_controller.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/system_event.h>

#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_lcd_co5300.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <cstdlib>
#include <cstring>
#include <new>

constexpr auto* TAG = "CO5300";

#define GET_CONFIG(device) (static_cast<const Co5300Config*>((device)->config))
#define GET_BACKLIGHT_CONFIG(device) (static_cast<const Co5300BacklightConfig*>((device)->config))

// QSPI commands encode their opcode in the top byte of the 32-bit command word.
#define CO5300_QSPI_OPCODE_WRITE_CMD (0x02U << 24)
#define CO5300_CMD_BRIGHTNESS 0x51
#define CO5300_CMD_TEAR_ON 0x35

// Wipe the panel's undefined power-up GRAM content in bands rather than one huge transfer.
constexpr int32_t CLEAR_BAND_LINES = 32;

// Consecutive missed TE pulses tolerated before tear sync is switched off for good.
constexpr uint8_t TE_MAX_MISSED_PULSES = 5;

// Successful TE waits after which the driver reports that tear sync is working.
constexpr uint32_t TE_STABLE_FRAMES = 120;

struct Co5300Internal {
    Device* spi_controller;
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    SemaphoreHandle_t draw_done_semaphore;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    // Optional TE (tearing effect) sync: the panel pulses this line at every vertical blank.
    GpioDescriptor* te_descriptor;
    SemaphoreHandle_t te_semaphore;
    uint32_t te_timeout_ms;
    bool te_enabled;
    uint8_t te_timeout_count;
    uint32_t te_success_count;
    bool te_stable_logged;
    // Heap-allocated only when the devicetree supplies a custom init sequence. Each entry's .data
    // points directly into the devicetree's static byte buffer, so only this array needs freeing.
    co5300_lcd_init_cmd_t* parsed_init_cmds;
};

static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void* user_ctx) {
    auto* internal = static_cast<Co5300Internal*>(user_ctx);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->draw_done_semaphore, &high_task_woken);
    return high_task_woken == pdTRUE;
}

// Runs in GPIO ISR context: signals the start of a new panel frame.
static void on_te_pulse(void* arg) {
    auto* internal = static_cast<Co5300Internal*>(arg);
    BaseType_t high_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(internal->te_semaphore, &high_task_woken);
    if (high_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static int pin_or_unused(const struct GpioPinSpec& pin) {
    return pin.gpio_controller == nullptr ? -1 : static_cast<int>(pin.pin);
}

// Unpacks the devicetree's flat [cmd, data_len, delay_ms, data_len bytes...] encoding (produced by
// the devicetree compiler's "array" property type) into a heap-allocated co5300_lcd_init_cmd_t
// array. Each entry's .data points into `bytes`, which is the devicetree's static const buffer and
// outlives the device.
static bool parse_init_sequence(const uint8_t* bytes, uint32_t length, co5300_lcd_init_cmd_t** out_cmds, uint16_t* out_count) {
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

    auto* cmds = static_cast<co5300_lcd_init_cmd_t*>(malloc(count * sizeof(co5300_lcd_init_cmd_t)));
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
    *out_count = static_cast<uint16_t>(count);
    return true;
}

// Sends an already-clamped region to the panel and blocks until the transfer physically completes.
static error_t present(Co5300Internal* internal, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* data) {
    xSemaphoreTake(internal->draw_done_semaphore, 0);

    spi_controller_lock(internal->spi_controller);

    // Start each transfer on a fresh panel frame. Without this the panel can scan a region while
    // it is still being written, which shows up as a diagonal shear/tearing across the update.
    if (internal->te_enabled) {
        // Discard a pulse that arrived while we were preparing, so we always wait for the next one.
        xSemaphoreTake(internal->te_semaphore, 0);
        if (xSemaphoreTake(internal->te_semaphore, pdMS_TO_TICKS(internal->te_timeout_ms)) == pdTRUE) {
            internal->te_timeout_count = 0;
            if (!internal->te_stable_logged && ++internal->te_success_count >= TE_STABLE_FRAMES) {
                // Reaching the console (USB enumerates before the panel comes up) would otherwise
                // hide the boot-time TE status, so report it once the UI is actually drawing.
                LOG_I(TAG, "TE sync stable after %u frame-aligned transfers", (unsigned)internal->te_success_count);
                internal->te_stable_logged = true;
            }
        } else if (++internal->te_timeout_count >= TE_MAX_MISSED_PULSES) {
            // A panel that never pulses (miswired TE, or held in sleep) would otherwise add the
            // full timeout to every flush. Drop sync instead of wrecking the frame rate.
            internal->te_enabled = false;
            LOG_W(TAG, "TE stopped pulsing; disabling tear sync");
        }
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(internal->panel_handle, x_start, y_start, x_end, y_end, data);
    if (ret != ESP_OK) {
        spi_controller_unlock(internal->spi_controller);
        return ERROR_RESOURCE;
    }

    // Hold the bus lock across the wait too: the command/data phases aren't wrapped in their own
    // acquire_bus by esp_lcd_panel_io_spi, so another bus user could otherwise interleave with a
    // transfer that's still in flight.
    xSemaphoreTake(internal->draw_done_semaphore, portMAX_DELAY);
    spi_controller_unlock(internal->spi_controller);
    return ERROR_NONE;
}

// region Driver lifecycle

// The panel keeps scanning the last frame after the CPU enters deep sleep, which looks like a
// frozen screen (and drains the battery), so blank it and put it into sleep-in first.
static void on_deep_sleep_prepare(SystemEvent*, void* context) {
    auto* device = static_cast<Device*>(context);
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    if (internal == nullptr || internal->panel_handle == nullptr) {
        return;
    }
    spi_controller_lock(internal->spi_controller);
    esp_lcd_panel_disp_on_off(internal->panel_handle, false);
    esp_lcd_panel_disp_sleep(internal->panel_handle, true);
    spi_controller_unlock(internal->spi_controller);
}

// Releases the optional TE input (interrupt, callback, pin) and its semaphore.
static void stop_te(Co5300Internal* internal) {
    internal->te_enabled = false;
    if (internal->te_descriptor != nullptr) {
        gpio_descriptor_disable_interrupt(internal->te_descriptor);
        gpio_descriptor_remove_callback(internal->te_descriptor);
        gpio_descriptor_release(internal->te_descriptor);
        internal->te_descriptor = nullptr;
    }
    if (internal->te_semaphore != nullptr) {
        vSemaphoreDelete(internal->te_semaphore);
        internal->te_semaphore = nullptr;
    }
}

// Sets up the optional TE input. Returns true when the panel's frame pulses can be consumed.
static bool start_te(Co5300Internal* internal, const Co5300Config* config) {
    if (config->pin_te.gpio_controller == nullptr) {
        return false;
    }

    internal->te_timeout_ms = config->te_timeout_ms > 0 ? config->te_timeout_ms : 50;
    internal->te_semaphore = xSemaphoreCreateBinary();
    if (internal->te_semaphore == nullptr) {
        return false;
    }

    gpio_flags_t flags = GPIO_FLAG_INTERRUPT_TO_OPTIONS(GPIO_FLAG_DIRECTION_INPUT, GPIO_INTERRUPT_POS_EDGE);
    internal->te_descriptor = gpio_descriptor_acquire(
        config->pin_te.gpio_controller, config->pin_te.pin, flags, GPIO_OWNER_GPIO
    );
    if (internal->te_descriptor == nullptr) {
        LOG_W(TAG, "Failed to acquire TE pin; running without tear sync");
        stop_te(internal);
        return false;
    }

    if (gpio_descriptor_add_callback(internal->te_descriptor, on_te_pulse, internal) != ERROR_NONE) {
        LOG_W(TAG, "Failed to add TE callback; running without tear sync");
        stop_te(internal);
        return false;
    }

    if (gpio_descriptor_enable_interrupt(internal->te_descriptor) != ERROR_NONE) {
        LOG_W(TAG, "Failed to enable TE interrupt; running without tear sync");
        stop_te(internal);
        return false;
    }

    return true;
}

// Sends the panel command that enables TE output. The caller must hold the SPI bus lock.
static bool enable_te_output(Co5300Internal* internal) {
    uint32_t tear_on_cmd = CO5300_QSPI_OPCODE_WRITE_CMD | (CO5300_CMD_TEAR_ON << 8);
    uint8_t te_mode = 0x00; // V-blank only
    if (esp_lcd_panel_io_tx_param(internal->io_handle, tear_on_cmd, &te_mode, 1) != ESP_OK) {
        return false;
    }
    internal->te_enabled = true;
    return true;
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

    auto* internal = static_cast<Co5300Internal*>(calloc(1, sizeof(Co5300Internal)));
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    internal->parsed_init_cmds = nullptr;
    internal->swap_xy = config->swap_xy;
    internal->mirror_x = config->mirror_x;
    internal->mirror_y = config->mirror_y;
    internal->spi_controller = parent;
    internal->te_descriptor = nullptr;
    internal->te_semaphore = nullptr;
    internal->te_timeout_ms = 0;
    internal->te_enabled = false;
    internal->te_timeout_count = 0;
    internal->te_success_count = 0;
    internal->te_stable_logged = false;

    internal->draw_done_semaphore = xSemaphoreCreateBinary();
    if (internal->draw_done_semaphore == nullptr) {
        free(internal);
        return ERROR_OUT_OF_MEMORY;
    }

    const co5300_lcd_init_cmd_t* init_cmds = nullptr;
    uint16_t init_cmds_size = 0;
    if (config->init_sequence != nullptr && config->init_sequence_length > 0) {
        if (!parse_init_sequence(config->init_sequence, config->init_sequence_length, &internal->parsed_init_cmds, &init_cmds_size)) {
            LOG_E(TAG, "Failed to parse init-sequence property");
            vSemaphoreDelete(internal->draw_done_semaphore);
            free(internal);
            return ERROR_INVALID_ARGUMENT;
        }
        init_cmds = internal->parsed_init_cmds;
    }

    // Built manually rather than with CO5300_PANEL_IO_QSPI_CONFIG(): its macro assigns the plain
    // int -1 to dc_gpio_num (gpio_num_t), which C++ rejects (unlike C).
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = static_cast<gpio_num_t>(pin_or_unused(cs_pin)),
        .dc_gpio_num = static_cast<gpio_num_t>(-1),
        .spi_mode = 0,
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
            // The LVGL color buffer lives in PSRAM (see PREFER_EXTERNAL_RAM). Without this the SPI
            // driver bounces every chunk through a freshly-allocated internal DMA buffer, which is
            // both slower and can fail when internal RAM is tight. ESP32-S31 can DMA from PSRAM.
            .psram_dma_direct = 1,
            .lsb_first = 0,
            .cs_high_active = 0,
        },
    };

    esp_err_t ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)spi_config->host, &io_config, &internal->io_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
        free(internal->parsed_init_cmds);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    co5300_vendor_config_t vendor_config = {
        .init_cmds = init_cmds,
        .init_cmds_size = init_cmds_size,
        .flags = {
            .use_mipi_interface = 0,
            .use_qspi_interface = 1,
        },
    };

    esp_lcd_panel_dev_config_t panel_config = {
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .reset_gpio_num = static_cast<gpio_num_t>(pin_or_unused(config->pin_reset)),
        .vendor_config = &vendor_config,
        // The CO5300 reset line is fixed active-low in hardware.
        .flags = { .reset_active_high = false },
    };

    ret = esp_lcd_new_panel_co5300(internal->io_handle, &panel_config, &internal->panel_handle);
    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to create panel: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(internal->io_handle);
        free(internal->parsed_init_cmds);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    // Set up the TE input before powering the panel up so the initial GRAM clear is frame-aligned too.
    bool te_ready = start_te(internal, config);

    spi_controller_lock(internal->spi_controller);
    bool ok =
        esp_lcd_panel_reset(internal->panel_handle) == ESP_OK &&
        esp_lcd_panel_init(internal->panel_handle) == ESP_OK;

    // Turn on the panel's TE output (V-blank pulses) so present() can align transfers to frames.
    // A failure here only loses tear sync, so the panel bring-up continues regardless.
    if (ok && te_ready) {
        if (!enable_te_output(internal)) {
            LOG_W(TAG, "Failed to enable panel TE output; running without tear sync");
            stop_te(internal);
            te_ready = false;
        } else {
            LOG_I(TAG, "TE sync enabled on GPIO%d", config->pin_te.pin);
        }
    }

    ok = ok &&
        esp_lcd_panel_set_gap(internal->panel_handle, config->gap_x, config->gap_y) == ESP_OK &&
        esp_lcd_panel_swap_xy(internal->panel_handle, config->swap_xy) == ESP_OK &&
        esp_lcd_panel_mirror(internal->panel_handle, config->mirror_x, config->mirror_y) == ESP_OK;
    if (ok && config->invert_color) {
        ok = esp_lcd_panel_invert_color(internal->panel_handle, true) == ESP_OK;
    }
    ok = ok && esp_lcd_panel_disp_on_off(internal->panel_handle, true) == ESP_OK;
    spi_controller_unlock(internal->spi_controller);

    if (!ok) {
        LOG_E(TAG, "Failed to bring up panel");
        esp_lcd_panel_del(internal->panel_handle);
        esp_lcd_panel_io_del(internal->io_handle);
        stop_te(internal);
        free(internal->parsed_init_cmds);
        vSemaphoreDelete(internal->draw_done_semaphore);
        free(internal);
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, internal);

    if (system_event_callback_add(KERNEL_EVENT_DEEP_SLEEP_PREPARE, on_deep_sleep_prepare, device) != ERROR_NONE) {
        LOG_W(TAG, "Failed to subscribe to deep sleep event");
    }

    // Wipe the panel's undefined power-up GRAM content. Done in horizontal bands so the panel IO
    // never queues several large PSRAM-direct chunks at once.
    const size_t clear_buffer_size = static_cast<size_t>(config->horizontal_resolution) * CLEAR_BAND_LINES * 2;
    auto* clear_buffer = static_cast<uint8_t*>(heap_caps_malloc(clear_buffer_size, MALLOC_CAP_DMA));
    if (clear_buffer != nullptr) {
        std::memset(clear_buffer, 0, clear_buffer_size);
        for (int32_t y = 0; y < config->vertical_resolution; y += CLEAR_BAND_LINES) {
            int32_t y_end = y + CLEAR_BAND_LINES;
            if (y_end > config->vertical_resolution) {
                y_end = config->vertical_resolution;
            }
            if (present(internal, 0, y, config->horizontal_resolution, y_end, clear_buffer) != ERROR_NONE) {
                LOG_W(TAG, "Failed to clear panel");
                break;
            }
        }
        free(clear_buffer);
    } else {
        LOG_W(TAG, "Failed to allocate clear buffer; leaving panel GRAM undefined");
    }

    LOG_I(TAG, "CO5300 initialized: %dx%d reset=%d te=%d pixel_clock=%d Hz", config->horizontal_resolution,
          config->vertical_resolution, pin_or_unused(config->pin_reset), pin_or_unused(config->pin_te), config->pixel_clock_hz);
    return ERROR_NONE;
}

static error_t stop(Device* device) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));

    system_event_callback_remove(KERNEL_EVENT_DEEP_SLEEP_PREPARE, on_deep_sleep_prepare);

    spi_controller_lock(internal->spi_controller);
    if (internal->panel_handle != nullptr) {
        if (esp_lcd_panel_del(internal->panel_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel");
            spi_controller_unlock(internal->spi_controller);
            return ERROR_RESOURCE;
        }
        internal->panel_handle = nullptr;
    }

    if (internal->io_handle != nullptr) {
        if (esp_lcd_panel_io_del(internal->io_handle) != ESP_OK) {
            LOG_E(TAG, "Failed to delete panel IO");
            spi_controller_unlock(internal->spi_controller);
            return ERROR_RESOURCE;
        }
        internal->io_handle = nullptr;
    }
    spi_controller_unlock(internal->spi_controller);

    vSemaphoreDelete(internal->draw_done_semaphore);
    stop_te(internal);
    free(internal->parsed_init_cmds);
    free(internal);
    device_set_driver_data(device, nullptr);
    return ERROR_NONE;
}

// endregion

// region DisplayApi

static error_t co5300_reset(Device* device) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_reset(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t co5300_init(Device* device) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_init(internal->panel_handle) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    // esp_lcd_panel_init() replays the vendor sequence, which can reset the TE output. Restore it
    // when tear sync was active before the re-initialization.
    if (result == ERROR_NONE && internal->te_enabled && !enable_te_output(internal)) {
        LOG_W(TAG, "Failed to restore panel TE output; disabling tear sync");
        stop_te(internal);
    }
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t co5300_draw_bitmap(Device* device, int32_t x_start, int32_t y_start, int32_t x_end, int32_t y_end, const void* color_data) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    const auto* config = GET_CONFIG(device);

    if (x_start < 0) x_start = 0;
    if (y_start < 0) y_start = 0;
    if (x_end > config->horizontal_resolution) x_end = config->horizontal_resolution;
    if (y_end > config->vertical_resolution) y_end = config->vertical_resolution;
    if (x_start >= x_end || y_start >= y_end) {
        return ERROR_NONE;
    }

    return present(internal, x_start, y_start, x_end, y_end, color_data);
}

static error_t co5300_mirror(Device* device, bool x_axis, bool y_axis) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_mirror(internal->panel_handle, x_axis, y_axis) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    if (result == ERROR_NONE) {
        internal->mirror_x = x_axis;
        internal->mirror_y = y_axis;
    }
    return result;
}

static error_t co5300_swap_xy(Device* device, bool swap_axes) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_swap_xy(internal->panel_handle, swap_axes) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    if (result == ERROR_NONE) {
        internal->swap_xy = swap_axes;
    }
    return result;
}

static bool co5300_get_swap_xy(Device* device) {
    return static_cast<Co5300Internal*>(device_get_driver_data(device))->swap_xy;
}

static bool co5300_get_mirror_x(Device* device) {
    return static_cast<Co5300Internal*>(device_get_driver_data(device))->mirror_x;
}

static bool co5300_get_mirror_y(Device* device) {
    return static_cast<Co5300Internal*>(device_get_driver_data(device))->mirror_y;
}

static error_t co5300_set_gap(Device* device, int32_t x_gap, int32_t y_gap) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_set_gap(internal->panel_handle, x_gap, y_gap) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static int32_t co5300_get_gap_x(Device* device) {
    return GET_CONFIG(device)->gap_x;
}

static int32_t co5300_get_gap_y(Device* device) {
    return GET_CONFIG(device)->gap_y;
}

static error_t co5300_invert_color(Device* device, bool invert_color_data) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_invert_color(internal->panel_handle, invert_color_data) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t co5300_disp_on_off(Device* device, bool on_off) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_disp_on_off(internal->panel_handle, on_off) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

static error_t co5300_disp_sleep(Device* device, bool sleep) {
    auto* internal = static_cast<Co5300Internal*>(device_get_driver_data(device));
    spi_controller_lock(internal->spi_controller);
    error_t result = esp_lcd_panel_disp_sleep(internal->panel_handle, sleep) == ESP_OK ? ERROR_NONE : ERROR_RESOURCE;
    spi_controller_unlock(internal->spi_controller);
    return result;
}

// The panel is big-endian while the OS is little-endian, so every pixel's two bytes are swapped.
static enum DisplayColorFormat co5300_get_color_format(Device*) {
    return DISPLAY_COLOR_FORMAT_RGB565_SWAPPED;
}

static uint16_t co5300_get_resolution_x(Device* device) {
    return GET_CONFIG(device)->horizontal_resolution;
}

static uint16_t co5300_get_resolution_y(Device* device) {
    return GET_CONFIG(device)->vertical_resolution;
}

struct BacklightSearch {
    Device* found;
};

static bool find_backlight_child(Device* child, void* context) {
    auto* search = static_cast<BacklightSearch*>(context);
    if (device_get_type(child) == &BACKLIGHT_TYPE) {
        search->found = child;
        return false;
    }
    return true;
}

static Device* find_backlight(Device* device) {
    BacklightSearch search = { nullptr };
    device_for_each_child(device, &search, find_backlight_child);
    return search.found;
}

static error_t co5300_get_backlight(Device* device, Device** backlight) {
    auto* found = find_backlight(device);
    if (found == nullptr) {
        return ERROR_NOT_SUPPORTED;
    }
    *backlight = found;
    return ERROR_NONE;
}

// BACKLIGHT availability is per-board (a board could wire a panel without using the panel's own
// brightness command), so it can't live in the driver-wide static capability mask.
static bool co5300_has_capability(Device* device, uint32_t capability) {
    uint32_t capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
        DISPLAY_CAPABILITY_CAP_SET_GAP | DISPLAY_CAPABILITY_INVERT_COLOR |
        DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLEEP |
        DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM;
    if (GET_CONFIG(device)->full_frame) {
        capabilities |= DISPLAY_CAPABILITY_REQUIRES_FULL_FRAME;
    }
    if (find_backlight(device) != nullptr) {
        capabilities |= DISPLAY_CAPABILITY_BACKLIGHT;
    }
    return (capabilities & capability) == capability;
}

static void co5300_get_shape(Device* device, DisplayShape* shape) {
    const auto* config = GET_CONFIG(device);
    *shape = { .shape = config->shape, .corner_radius = config->corner_radius };
}

static const DisplayApi co5300_display_api = {
    .capabilities = DISPLAY_CAPABILITY_CAP_MIRROR | DISPLAY_CAPABILITY_CAP_SWAP_XY |
        DISPLAY_CAPABILITY_CAP_SET_GAP | DISPLAY_CAPABILITY_INVERT_COLOR |
        DISPLAY_CAPABILITY_ON_OFF | DISPLAY_CAPABILITY_SLEEP |
        DISPLAY_CAPABILITY_PREFER_EXTERNAL_RAM,
    .reset = co5300_reset,
    .init = co5300_init,
    .draw_bitmap = co5300_draw_bitmap,
    .clear = nullptr,
    .refresh = nullptr,
    .mirror = co5300_mirror,
    .swap_xy = co5300_swap_xy,
    .get_swap_xy = co5300_get_swap_xy,
    .get_mirror_x = co5300_get_mirror_x,
    .get_mirror_y = co5300_get_mirror_y,
    .set_gap = co5300_set_gap,
    .get_gap_x = co5300_get_gap_x,
    .get_gap_y = co5300_get_gap_y,
    .invert_color = co5300_invert_color,
    .disp_on_off = co5300_disp_on_off,
    .disp_sleep = co5300_disp_sleep,
    .get_color_format = co5300_get_color_format,
    .get_resolution_x = co5300_get_resolution_x,
    .get_resolution_y = co5300_get_resolution_y,
    // No direct frame buffer access: the panel streams from an LVGL-owned buffer.
    .get_frame_buffer = nullptr,
    .get_frame_buffer_count = nullptr,
    .get_backlight = co5300_get_backlight,
    .has_capability = co5300_has_capability,
    .get_shape = co5300_get_shape,
};

Driver co5300_driver = {
    .name = "co5300",
    .compatible = (const char*[]) { "chipone,co5300", nullptr },
    .start_device = start,
    .stop_device = stop,
    .probe = nullptr,
    .api = &co5300_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr
};

// endregion

// region Backlight

struct Co5300BacklightInternal {
    uint8_t brightness;
};

static error_t co5300_backlight_set_brightness(Device* device, uint8_t brightness) {
    // The backlight is a child of the display it dims.
    auto* display = device_get_parent(device);
    if (display == nullptr) {
        return ERROR_INVALID_STATE;
    }

    auto* display_internal = static_cast<Co5300Internal*>(device_get_driver_data(display));
    if (display_internal == nullptr || display_internal->io_handle == nullptr) {
        return ERROR_INVALID_STATE;
    }

    uint32_t lcd_cmd = CO5300_CMD_BRIGHTNESS;
    lcd_cmd <<= 8;
    lcd_cmd |= CO5300_QSPI_OPCODE_WRITE_CMD;

    spi_controller_lock(display_internal->spi_controller);
    esp_err_t ret = esp_lcd_panel_io_tx_param(display_internal->io_handle, lcd_cmd, &brightness, 1);
    spi_controller_unlock(display_internal->spi_controller);

    if (ret != ESP_OK) {
        LOG_E(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    static_cast<Co5300BacklightInternal*>(device_get_driver_data(device))->brightness = brightness;
    return ERROR_NONE;
}

static error_t co5300_backlight_set_brightness_default(Device* device) {
    return co5300_backlight_set_brightness(device, GET_BACKLIGHT_CONFIG(device)->brightness_default);
}

static error_t co5300_backlight_get_brightness(Device* device, uint8_t* out_brightness) {
    *out_brightness = static_cast<Co5300BacklightInternal*>(device_get_driver_data(device))->brightness;
    return ERROR_NONE;
}

static uint8_t co5300_backlight_get_min_brightness(Device*) {
    return 0;
}

static uint8_t co5300_backlight_get_max_brightness(Device*) {
    return 255;
}

static const BacklightApi CO5300_BACKLIGHT_API = {
    .set_brightness = co5300_backlight_set_brightness,
    .set_brightness_default = co5300_backlight_set_brightness_default,
    .get_brightness = co5300_backlight_get_brightness,
    .get_min_brightness = co5300_backlight_get_min_brightness,
    .get_max_brightness = co5300_backlight_get_max_brightness,
};

static error_t co5300_backlight_start(Device* device) {
    if (device_get_parent(device) == nullptr) {
        return ERROR_INVALID_STATE;
    }

    auto* internal = new(std::nothrow) Co5300BacklightInternal { .brightness = 0 };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    device_set_driver_data(device, internal);

    // Allowed to fail: the panel may not be initialized yet when children start, and a later
    // set_brightness_default() call can retry.
    co5300_backlight_set_brightness_default(device);

    return ERROR_NONE;
}

static error_t co5300_backlight_stop(Device* device) {
    co5300_backlight_set_brightness(device, 0); // Allowed to fail

    auto* internal = static_cast<Co5300BacklightInternal*>(device_get_driver_data(device));
    device_set_driver_data(device, nullptr);
    delete internal;

    return ERROR_NONE;
}

Driver co5300_backlight_driver = {
    .name = "co5300_backlight",
    .compatible = (const char*[]) { "co5300-backlight", nullptr },
    .start_device = co5300_backlight_start,
    .stop_device = co5300_backlight_stop,
    .probe = nullptr,
    .api = &CO5300_BACKLIGHT_API,
    .device_type = &BACKLIGHT_TYPE,
    .internal = nullptr
};

// endregion
