#include "Axs15231bDisplay.h"

#include <Tactility/Logger.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Axs15231b/Axs15231bTouch.h>
#include <EspLcdDisplayDriver.h>

#include <esp_lcd_axs15231b.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_ops.h>
#include <esp_heap_caps.h>

static const auto LOGGER = tt::Logger("AXS15231B");

static const axs15231b_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5}, 8, 0},
    {0xA0, (uint8_t[]){0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00}, 17, 0},
    {0xA2, (uint8_t[]){0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xf9, 0x10, 0x02, 0xff, 0xff, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A}, 31, 0},
    {0xD0, (uint8_t[]){0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12}, 30, 0},
    {0xA3, (uint8_t[]){0xA0, 0x06, 0xAa, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55}, 22, 0},
    {0xC1, (uint8_t[]){0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0d, 0x00, 0xFF, 0x40}, 30, 0},
    {0xC3, (uint8_t[]){0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01}, 11, 0},
    {0xC4, (uint8_t[]){0x00, 0x24, 0x33, 0x80, 0x00, 0xea, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50}, 29, 0},
    {0xC5, (uint8_t[]){0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00}, 23, 0},
    {0xC6, (uint8_t[]){0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22}, 20, 0},
    {0xC7, (uint8_t[]){0x50, 0x32, 0x28, 0x00, 0xa2, 0x80, 0x8f, 0x00, 0x80, 0xff, 0x07, 0x11, 0x9c, 0x67, 0xff, 0x24, 0x0c, 0x0d, 0x0e, 0x0f}, 20, 0},
    {0xC9, (uint8_t[]){0x33, 0x44, 0x44, 0x01}, 4, 0},
    {0xCF, (uint8_t[]){0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08}, 27, 0},
    {0xD5, (uint8_t[]){0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00}, 30, 0},
    {0xD6, (uint8_t[]){0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00}, 30, 0},
    {0xD7, (uint8_t[]){0x03, 0x01, 0x0b, 0x09, 0x0f, 0x0d, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F}, 19, 0},
    {0xD8, (uint8_t[]){0x02, 0x00, 0x0a, 0x08, 0x0e, 0x0c, 0x1E, 0x1F, 0x18, 0x1d, 0x1f, 0x19}, 12, 0},
    {0xD9, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDD, (uint8_t[]){0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F}, 12, 0},
    {0xDF, (uint8_t[]){0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90}, 8, 0},
    {0xE0, (uint8_t[]){0x3B, 0x28, 0x10, 0x16, 0x0c, 0x06, 0x11, 0x28, 0x5c, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D}, 17, 0},
    {0xE1, (uint8_t[]){0x37, 0x28, 0x10, 0x16, 0x0b, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F}, 17, 0},
    {0xE2, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE3, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F}, 17, 0},
    {0xE4, (uint8_t[]){0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D}, 17, 0},
    {0xE5, (uint8_t[]){0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F}, 17, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30}, 16, 0},
    {0xA4, (uint8_t[]){0x85, 0x85, 0x95, 0x85}, 4, 0},
    {0xBB, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 8, 0},
    {0x13, (uint8_t[]){0x00}, 0, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},   // Enable Tearing Effect output (V-blank sync)
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x2C, (uint8_t[]){0x00, 0x00, 0x00, 0x00}, 4, 0}
};

void Axs15231bDisplay::lvgl_port_flush_callback(lv_display_t *drv, const lv_area_t *area, uint8_t *color_map) {
    Axs15231bDisplay* self = (Axs15231bDisplay*)lv_display_get_user_data(drv);
    assert(self != nullptr);

    assert(drv != nullptr);
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_driver_data(drv);
    assert(panel_handle != nullptr);

    // Swap RGB565 byte order for SPI transmission (big-endian wire format)
    lv_draw_sw_rgb565_swap(color_map, lv_area_get_size(area));

    lv_display_rotation_t rotation = lv_display_get_rotation(drv);
    int logical_width = lv_display_get_horizontal_resolution(drv);
    int logical_height = lv_display_get_vertical_resolution(drv);

    uint16_t* draw_buf = (uint16_t*)color_map;

    if (rotation != LV_DISPLAY_ROTATION_0) {
        // Lazy-allocate tempBuf on first rotated frame
        static bool allocationErrorLogged = false;
        if (self->tempBuf == nullptr) {
            self->tempBuf = (uint16_t *)heap_caps_malloc(
                self->configuration->horizontalResolution * self->configuration->verticalResolution * sizeof(uint16_t),
                MALLOC_CAP_SPIRAM);
            if (self->tempBuf == nullptr) {
                if (!allocationErrorLogged) {
                    LOGGER.error("Failed to allocate rotation buffer, drawing unrotated");
                    allocationErrorLogged = true;
                }
                if (esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, self->configuration->horizontalResolution, self->configuration->verticalResolution, draw_buf) != ESP_OK) {
                    lv_display_flush_ready(drv);
                }
                return;
            }
        }

        // Rotate to tempBuf using tile-based approach for cache efficiency.
        // SPIRAM random access is very slow due to cache thrashing. By processing
        // in small tiles (TILE x TILE pixels), the working set stays within the
        // ESP32-S3's 32KB PSRAM cache, dramatically reducing cache misses.
        constexpr int TILE = 32;
        uint16_t* src = (uint16_t*)color_map;
        uint16_t* dst = self->tempBuf;
        const int hw = self->configuration->horizontalResolution;

        switch (rotation) {
            case LV_DISPLAY_ROTATION_90:
                for (int ty = 0; ty < logical_height; ty += TILE) {
                    for (int tx = 0; tx < logical_width; tx += TILE) {
                        int y_end = (ty + TILE < logical_height) ? ty + TILE : logical_height;
                        int x_end = (tx + TILE < logical_width) ? tx + TILE : logical_width;
                        for (int y = ty; y < y_end; y++) {
                            for (int x = tx; x < x_end; x++) {
                                dst[(logical_width - 1 - x) * hw + y] = src[y * logical_width + x];
                            }
                        }
                    }
                }
                break;
            case LV_DISPLAY_ROTATION_180: {
                // 180° is a simple reverse - sequential reads and writes
                const int total = logical_width * logical_height;
                for (int i = 0; i < total; i++) {
                    dst[total - 1 - i] = src[i];
                }
                break;
            }
            case LV_DISPLAY_ROTATION_270:
                for (int ty = 0; ty < logical_height; ty += TILE) {
                    for (int tx = 0; tx < logical_width; tx += TILE) {
                        int y_end = (ty + TILE < logical_height) ? ty + TILE : logical_height;
                        int x_end = (tx + TILE < logical_width) ? tx + TILE : logical_width;
                        for (int y = ty; y < y_end; y++) {
                            for (int x = tx; x < x_end; x++) {
                                dst[x * hw + (logical_height - 1 - y)] = src[y * logical_width + x];
                            }
                        }
                    }
                }
                break;
            default:
                break;
        }
        draw_buf = self->tempBuf;
    }

    // Wait for TE (tearing effect) signal before starting SPI transfer.
    // This synchronizes frame output with the display's V-blank to reduce tearing.
    if (self->teSyncSemaphore != nullptr) {
        xSemaphoreTake(self->teSyncSemaphore, 0);          // Drain any pending signal
        xSemaphoreTake(self->teSyncSemaphore, pdMS_TO_TICKS(20)); // Wait for next V-blank
    }

    esp_err_t ret = esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, self->configuration->horizontalResolution, self->configuration->verticalResolution, draw_buf);
    if (ret != ESP_OK) {
        // If SPI transfer failed, on_color_trans_done won't fire.
        // Manually signal flush ready to prevent LVGL from hanging.
        LOGGER.error("draw_bitmap failed: {}", esp_err_to_name(ret));
        lv_display_flush_ready(drv);
    }
}

bool Axs15231bDisplay::onColorTransDone(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx) {
    lv_display_t *disp = (lv_display_t *)user_ctx;
    if (disp != nullptr) {
        lv_display_flush_ready(disp);
    }
    return false;
}

void IRAM_ATTR Axs15231bDisplay::teIsrHandler(void* arg) {
    SemaphoreHandle_t sem = (SemaphoreHandle_t)arg;
    BaseType_t needYield = pdFALSE;
    xSemaphoreGiveFromISR(sem, &needYield);
    if (needYield == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

bool Axs15231bDisplay::setupTeSync() {
    if (configuration->tePin == GPIO_NUM_NC) {
        LOGGER.info("TE pin not configured, skipping TE sync");
        return true;
    }

    teSyncSemaphore = xSemaphoreCreateBinary();
    if (teSyncSemaphore == nullptr) {
        LOGGER.error("Failed to create TE sync semaphore");
        return false;
    }

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pin_bit_mask = (1ULL << configuration->tePin);

    if (gpio_config(&io_conf) != ESP_OK) {
        LOGGER.error("Failed to configure TE GPIO");
        vSemaphoreDelete(teSyncSemaphore);
        teSyncSemaphore = nullptr;
        return false;
    }

    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err == ESP_OK) {
        isrServiceInstalledByUs = true;
    } else if (err != ESP_ERR_INVALID_STATE) {
        LOGGER.error("Failed to install GPIO ISR service");
        vSemaphoreDelete(teSyncSemaphore);
        teSyncSemaphore = nullptr;
        return false;
    }

    if (gpio_isr_handler_add(configuration->tePin, teIsrHandler, (void*)teSyncSemaphore) != ESP_OK) {
        LOGGER.error("Failed to add TE ISR handler");
        gpio_intr_disable(configuration->tePin);
        if (isrServiceInstalledByUs) {
            gpio_uninstall_isr_service();
            isrServiceInstalledByUs = false;
        }
        vSemaphoreDelete(teSyncSemaphore);
        teSyncSemaphore = nullptr;
        return false;
    }

    teIsrInstalled = true;
    LOGGER.info("TE sync enabled on GPIO {}", (int)configuration->tePin);
    return true;
}

void Axs15231bDisplay::teardownTeSync() {
    if (teIsrInstalled && configuration->tePin != GPIO_NUM_NC) {
        gpio_isr_handler_remove(configuration->tePin);
        gpio_intr_disable(configuration->tePin);
        teIsrInstalled = false;
    }
    if (isrServiceInstalledByUs) {
        gpio_uninstall_isr_service();
        isrServiceInstalledByUs = false;
    }
    if (teSyncSemaphore != nullptr) {
        vSemaphoreDelete(teSyncSemaphore);
        teSyncSemaphore = nullptr;
    }
}

bool Axs15231bDisplay::createIoHandle() {
    const esp_lcd_panel_io_spi_config_t panel_io_config = {
        .cs_gpio_num = configuration->csPin,
        .dc_gpio_num = configuration->dcPin,
        .spi_mode = 3,
        .pclk_hz = configuration->pixelClockFrequency,
        .trans_queue_depth = configuration->transactionQueueDepth,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
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
            .cs_high_active = 0
        }
    };

    return esp_lcd_new_panel_io_spi(configuration->spiHostDevice, &panel_io_config, &ioHandle) == ESP_OK;
}

bool Axs15231bDisplay::createPanelHandle() {
    const axs15231b_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = configuration->resetPin,
        .rgb_ele_order = configuration->rgbElementOrder,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
        .bits_per_pixel = 16,
        .flags = {
            .reset_active_high = false
        },
        .vendor_config = (void *)&vendor_config
    };

    if (esp_lcd_new_panel_axs15231b(ioHandle, &panel_config, &panelHandle) != ESP_OK) {
        LOGGER.error("Failed to create axs15231b");
        return false;
    }

    if (esp_lcd_panel_reset(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to reset panel");
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        return false;
    }

    if (esp_lcd_panel_init(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to init panel");
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        return false;
    }

    //SWAPXY Doesn't work with the JC3248W535... Left in for future compatibility.
    if (esp_lcd_panel_swap_xy(panelHandle, configuration->swapXY) != ESP_OK) {
        LOGGER.error("Failed to swap XY");
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        return false;
    }

    if (esp_lcd_panel_mirror(panelHandle, configuration->mirrorX, configuration->mirrorY) != ESP_OK) {
        LOGGER.error("Failed to mirror panel");
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        return false;
    }

    if (esp_lcd_panel_invert_color(panelHandle, configuration->invertColor) != ESP_OK) {
        LOGGER.error("Failed to invert color");
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        return false;
    }

    if (esp_lcd_panel_disp_on_off(panelHandle, false) != ESP_OK) {
        LOGGER.error("Failed to turn off panel");
        esp_lcd_panel_del(panelHandle);
        panelHandle = nullptr;
        return false;
    }

    return true;
}

Axs15231bDisplay::Axs15231bDisplay(std::unique_ptr<Configuration> inConfiguration) :
    configuration(std::move(inConfiguration))
{
    assert(configuration != nullptr);
}

bool Axs15231bDisplay::start() {
    if (!createIoHandle()) {
        LOGGER.error("Failed to create IO handle");
        return false;
    }

    if (!createPanelHandle()) {
        LOGGER.error("Failed to create panel handle");
        esp_lcd_panel_io_del(ioHandle);
        ioHandle = nullptr;
        return false;
    }

    if (!setupTeSync()) {
        LOGGER.warn("TE sync setup failed, continuing without TE synchronization");
    }

    return true;
}

bool Axs15231bDisplay::stop() {
    if (lvglDisplay != nullptr) {
        stopLvgl();
    }

    teardownTeSync();

    // Invalidate cached DisplayDriver - it holds a raw panelHandle that is about to be deleted
    displayDriver.reset();

    if (panelHandle != nullptr && esp_lcd_panel_del(panelHandle) != ESP_OK) {
        LOGGER.error("Failed to delete panel");
    }
    panelHandle = nullptr;

    if (ioHandle != nullptr && esp_lcd_panel_io_del(ioHandle) != ESP_OK) {
        LOGGER.error("Failed to delete IO");
    }
    ioHandle = nullptr;

    return true;
}

bool Axs15231bDisplay::startLvgl() {
    if (lvglDisplay != nullptr) {
        LOGGER.error("LVGL already started");
        return false;
    }

    lvglDisplay = lv_display_create(configuration->horizontalResolution, configuration->verticalResolution);
    lv_display_set_user_data(lvglDisplay, this);
    lv_display_set_color_format(lvglDisplay, LV_COLOR_FORMAT_RGB565);

    uint32_t buffer_pixels = configuration->bufferSize;
    uint32_t bufferSize = buffer_pixels * sizeof(uint16_t);
    buffer1 = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
    buffer2 = (uint16_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
    if (buffer1 == nullptr || buffer2 == nullptr) {
        LOGGER.error("Failed to allocate buffers");
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        buffer1 = nullptr;
        buffer2 = nullptr;
        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
        return false;
    }

    lv_display_set_buffers(lvglDisplay, buffer1, buffer2, bufferSize, LV_DISPLAY_RENDER_MODE_FULL);

    lv_display_set_flush_cb(lvglDisplay, lvgl_port_flush_callback);

    lv_display_set_driver_data(lvglDisplay, panelHandle);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = onColorTransDone,
    };
    if (esp_lcd_panel_io_register_event_callbacks(ioHandle, &cbs, lvglDisplay) != ESP_OK) {
        LOGGER.error("Failed to register panel IO callbacks");
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        buffer1 = nullptr;
        buffer2 = nullptr;
        lv_display_delete(lvglDisplay);
        lvglDisplay = nullptr;
        return false;
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr && touch_device->supportsLvgl()) {
        touch_device->startLvgl(lvglDisplay);
    }

    return true;
}

bool Axs15231bDisplay::stopLvgl() {
    if (lvglDisplay == nullptr) {
        return false;
    }

    auto touch_device = getTouchDevice();
    if (touch_device != nullptr) {
        touch_device->stopLvgl();
    }

    esp_lcd_panel_disp_on_off(panelHandle, false);

    // Unregister IO callbacks before deleting the display to prevent use-after-free.
    // The on_color_trans_done callback captures lvglDisplay as user_ctx; if a DMA
    // transfer completes after lv_display_delete(), it would call
    // lv_display_flush_ready() on a freed pointer.
    const esp_lcd_panel_io_callbacks_t nullCbs = {
        .on_color_trans_done = nullptr,
    };
    esp_lcd_panel_io_register_event_callbacks(ioHandle, &nullCbs, nullptr);

    lv_display_delete(lvglDisplay);
    lvglDisplay = nullptr;

    heap_caps_free(buffer1);
    heap_caps_free(buffer2);
    buffer1 = nullptr;
    buffer2 = nullptr;

    heap_caps_free(tempBuf);
    tempBuf = nullptr;

    return true;
}

std::shared_ptr<tt::hal::display::DisplayDriver> Axs15231bDisplay::getDisplayDriver() {
    if (lvglDisplay != nullptr) {
        LOGGER.error("Cannot get DisplayDriver while LVGL is active - call stopLvgl() first");
        return nullptr;
    }
    if (panelHandle == nullptr) {
        LOGGER.error("Cannot get DisplayDriver - display is not started");
        return nullptr;
    }
    if (displayDriver == nullptr) {
        displayDriver = std::make_shared<EspLcdDisplayDriver>(
            panelHandle,
            configuration->horizontalResolution,
            configuration->verticalResolution,
            tt::hal::display::ColorFormat::RGB565Swapped
        );
    }
    return displayDriver;
}
