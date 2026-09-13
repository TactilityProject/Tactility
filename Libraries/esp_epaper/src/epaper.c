#include "epaper.h"
#include "epaper_panel.h"
#include "epaper_spi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "epaper";

/*=============================================================================
 * Device Structure
 *============================================================================*/

struct epd_device {
    epd_config_t config;
    epd_spi_t spi;
    const epd_panel_desc_t *panel;
    const epd_controller_ops_t *ops;
    uint16_t width;
    uint16_t height;
    uint32_t buffer_size;
    uint8_t *framebuffer;
    bool initialized;
    bool partial_ready;
    uint32_t partial_count;
};

/*=============================================================================
 * Device Access Functions (used by controllers)
 *============================================================================*/

epd_spi_t* epd_get_spi(epd_device_t *dev)
{
    return dev ? &dev->spi : NULL;
}

uint16_t epd_get_width(epd_device_t *dev)
{
    return dev ? dev->width : 0;
}

uint16_t epd_get_height(epd_device_t *dev)
{
    return dev ? dev->height : 0;
}

const epd_panel_desc_t* epd_get_panel(epd_device_t *dev)
{
    return dev ? dev->panel : NULL;
}

bool epd_is_partial_ready(epd_device_t *dev)
{
    return dev ? dev->partial_ready : false;
}

/*=============================================================================
 * Initialization
 *============================================================================*/

esp_err_t epd_init(const epd_config_t *config, epd_handle_t *handle)
{
    if (!config || !handle) {
        return ESP_ERR_INVALID_ARG;
    }

    // Get panel descriptor from registry
    const epd_panel_desc_t *panel = epd_get_panel_desc(config->panel.type);
    if (!panel) {
        ESP_LOGE(TAG, "Unknown panel type: %d", config->panel.type);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Get controller operations
    const epd_controller_ops_t *ops = epd_get_controller_ops(panel->ctrl);
    if (!ops) {
        ESP_LOGE(TAG, "Unknown controller type: %d", panel->ctrl);
        return ESP_ERR_NOT_SUPPORTED;
    }

    // Allocate device
    epd_device_t *dev = calloc(1, sizeof(epd_device_t));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }

    // Copy config and set references
    memcpy(&dev->config, config, sizeof(epd_config_t));
    dev->panel = panel;
    dev->ops = ops;

    // Set dimensions (allow override from config)
    dev->width = config->panel.width > 0 ? config->panel.width : panel->width;
    dev->height = config->panel.height > 0 ? config->panel.height : panel->height;

    // Calculate buffer size using panel's bits_per_pixel
    dev->buffer_size = epd_calc_buffer_size(dev->width, dev->height, panel->bits_per_pixel);

    // Allocate framebuffer (prefer PSRAM for large buffers)
    if (dev->buffer_size > 32768) {
        dev->framebuffer = heap_caps_malloc(dev->buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    } else {
        dev->framebuffer = heap_caps_malloc(dev->buffer_size, MALLOC_CAP_DMA);
    }

    if (!dev->framebuffer) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer (%lu bytes)", dev->buffer_size);
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Framebuffer allocated: %lu bytes", dev->buffer_size);
    memset(dev->framebuffer, 0xFF, dev->buffer_size);  // White

    // Initialize SPI
    esp_err_t ret = epd_spi_init(&dev->spi, &config->pins, &config->spi);
    if (ret != ESP_OK) {
        free(dev->framebuffer);
        free(dev);
        return ret;
    }

    // Initialize panel via controller
    ret = dev->ops->init(dev);
    if (ret != ESP_OK) {
        epd_spi_deinit(&dev->spi);
        free(dev->framebuffer);
        free(dev);
        return ret;
    }

    dev->initialized = true;
    *handle = dev;

    ESP_LOGI(TAG, "Initialized %s (%dx%d)", panel->name, dev->width, dev->height);
    return ESP_OK;
}

esp_err_t epd_deinit(epd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;

    if (dev->ops && dev->ops->sleep) {
        dev->ops->sleep(dev);
    }

    epd_spi_deinit(&dev->spi);

    if (dev->framebuffer) {
        free(dev->framebuffer);
    }

    free(dev);
    return ESP_OK;
}

/*=============================================================================
 * Info & Status
 *============================================================================*/

esp_err_t epd_get_info(epd_handle_t handle, epd_panel_info_t *info)
{
    if (!handle || !info) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    info->width = dev->width;
    info->height = dev->height;
    info->buffer_size = dev->buffer_size;
    info->color_mode = dev->panel->color_mode;
    return ESP_OK;
}

bool epd_is_busy(epd_handle_t handle)
{
    if (!handle) return false;
    epd_device_t *dev = handle;
    return epd_spi_is_busy(&dev->spi) != 0;
}

esp_err_t epd_wait_busy(epd_handle_t handle, uint32_t timeout_ms)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    epd_device_t *dev = handle;
    epd_spi_wait_busy(&dev->spi, timeout_ms);
    return ESP_OK;
}

/*=============================================================================
 * Display Operations
 *============================================================================*/

esp_err_t epd_clear(epd_handle_t handle)
{
    return epd_fill(handle, 0xFF);
}

esp_err_t epd_fill(epd_handle_t handle, uint8_t color)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    memset(dev->framebuffer, color, dev->buffer_size);

    return epd_update(handle, dev->framebuffer, EPD_UPDATE_FULL);
}

esp_err_t epd_update(epd_handle_t handle, const uint8_t *buffer, epd_update_mode_t mode)
{
    if (!handle || !buffer) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    esp_err_t ret;

    // Check capabilities and fallback if needed
    if (mode == EPD_UPDATE_PARTIAL && !epd_panel_has_cap(dev->panel, EPD_CAP_PARTIAL)) {
        ESP_LOGD(TAG, "Partial not supported, using full refresh");
        mode = EPD_UPDATE_FULL;
    }
    if (mode == EPD_UPDATE_FAST && !epd_panel_has_cap(dev->panel, EPD_CAP_FAST)) {
        ESP_LOGD(TAG, "Fast not supported, using full refresh");
        mode = EPD_UPDATE_FULL;
    }

    // Periodic full refresh: repeated partial writes on the same GDDRAM base
    // image accumulate ghosting, so every Nth partial refresh the whole panel.
    // The conversion redraws the identical current buffer, so the base image
    // stays valid and subsequent partials can continue immediately.
    bool converted_to_full = false;
    if (mode == EPD_UPDATE_PARTIAL && dev->partial_ready && dev->panel->full_refresh_interval > 0) {
        if (++dev->partial_count >= dev->panel->full_refresh_interval) {
            dev->partial_count = 0;
            mode = EPD_UPDATE_FULL;
            converted_to_full = true;
            ESP_LOGI(TAG, "Periodic full refresh after %d partial updates", dev->panel->full_refresh_interval);
        }
    }

    // Copy to framebuffer
    memcpy(dev->framebuffer, buffer, dev->buffer_size);

    // For partial mode on first call, do a full refresh to set base image
    if (mode == EPD_UPDATE_PARTIAL && !dev->partial_ready) {
        if (!dev->initialized) {
            ret = dev->ops->init(dev);
            if (ret != ESP_OK) return ret;
            dev->initialized = true;
        }

        ret = dev->ops->write_ram(dev, buffer, dev->buffer_size);
        if (ret != ESP_OK) return ret;

        ret = dev->ops->update(dev, EPD_UPDATE_FULL);
        if (ret != ESP_OK) return ret;

        dev->partial_ready = true;
        ESP_LOGI(TAG, "Partial mode ready");
        return ESP_OK;
    }

    // Full refresh resets partial mode (unless it was the periodic conversion,
    // which refreshed the same base image and keeps partial mode valid)
    if (mode == EPD_UPDATE_FULL && !converted_to_full) {
        dev->partial_ready = false;
        if (!dev->initialized) {
            ret = dev->ops->init(dev);
            if (ret != ESP_OK) return ret;
            dev->initialized = true;
        }
    }

    // Write RAM and update
    ret = dev->ops->write_ram(dev, buffer, dev->buffer_size);
    if (ret != ESP_OK) return ret;

    return dev->ops->update(dev, mode);
}

esp_err_t epd_update_partial(epd_handle_t handle, uint16_t x, uint16_t y,
                              uint16_t w, uint16_t h, const uint8_t *buffer)
{
    if (!handle || !buffer) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;

    if (!epd_panel_has_cap(dev->panel, EPD_CAP_PARTIAL)) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!dev->ops->write_ram_partial) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t ret = dev->ops->write_ram_partial(dev, x, y, w, h, buffer);
    if (ret != ESP_OK) return ret;

    return dev->ops->update(dev, EPD_UPDATE_PARTIAL);
}

esp_err_t epd_set_base_image(epd_handle_t handle, const uint8_t *buffer)
{
    if (!handle || !buffer) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    memcpy(dev->framebuffer, buffer, dev->buffer_size);
    return ESP_OK;
}

/*=============================================================================
 * Power Management
 *============================================================================*/

esp_err_t epd_sleep(epd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    if (dev->ops->sleep) {
        // Deep sleep clears GDDRAM, so the base image for partial updates is
        // lost; the next partial update must re-establish it with a full refresh.
        dev->partial_ready = false;
        dev->partial_count = 0;
        dev->initialized = false;
        return dev->ops->sleep(dev);
    }
    return ESP_OK;
}

esp_err_t epd_wake(epd_handle_t handle)
{
    if (!handle) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    if (dev->ops->wake) {
        esp_err_t ret = dev->ops->wake(dev);
        if (ret == ESP_OK) {
            dev->initialized = true;
            // Wake re-initializes the panel and clears GDDRAM, so the next
            // partial update must re-establish the base with a full refresh.
            dev->partial_ready = false;
            dev->partial_count = 0;
        }
        return ret;
    }
    return dev->ops->init(dev);
}

/*=============================================================================
 * Framebuffer Access
 *============================================================================*/

uint8_t* epd_get_framebuffer(epd_handle_t handle)
{
    if (!handle) return NULL;
    return handle->framebuffer;
}

esp_err_t epd_flush_framebuffer(epd_handle_t handle, epd_update_mode_t mode)
{
    if (!handle) return ESP_ERR_INVALID_ARG;
    return epd_update(handle, handle->framebuffer, mode);
}

esp_err_t epd_lvgl_flush(epd_handle_t handle, const void *area, const uint8_t *px_map)
{
    if (!handle || !px_map) return ESP_ERR_INVALID_ARG;

    epd_device_t *dev = handle;
    memcpy(dev->framebuffer, px_map, dev->buffer_size);
    return ESP_OK;
}
