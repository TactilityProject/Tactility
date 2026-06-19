#include <sdkconfig.h>
#ifdef CONFIG_SOC_USB_OTG_SUPPORTED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbhost.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <usb/usb_host.h>
#include <esp_intr_alloc.h>

#define TAG "esp32_usbhost"

#define GET_CONFIG(device) ((const Esp32UsbHostConfig*)(device)->config)

constexpr auto USB_LIB_TASK_STACK        = 4096;
constexpr auto USB_LIB_TASK_PRIORITY     = 10;
constexpr auto USB_LIB_EVENT_TIMEOUT_MS  = 500;
constexpr auto USB_HOST_STOP_TIMEOUT_MS  = 3000;
constexpr auto USB_HOST_STOP_RETRY_MS    = 1000;

struct UsbHostContext {
    TaskHandle_t      lib_task     = nullptr;
    SemaphoreHandle_t lib_task_done = nullptr;
};

static void usbLibTask(void* arg) {
    auto* ctx = static_cast<UsbHostContext*>(arg);
    LOG_I(TAG, "lib task started");

    while (true) {
        uint32_t flags = 0;
        esp_err_t err = usb_host_lib_handle_events(pdMS_TO_TICKS(USB_LIB_EVENT_TIMEOUT_MS), &flags);
        if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
            LOG_W(TAG, "usb_host_lib_handle_events: %s", esp_err_to_name(err));
        }

        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            LOG_I(TAG, "no more USB clients, freeing all devices");
            usb_host_device_free_all();
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            LOG_I(TAG, "all USB devices freed");
        }

        if (ulTaskNotifyTake(pdFALSE, 0) > 0) {
            break;
        }
    }

    LOG_I(TAG, "lib task stopping");
    xSemaphoreGive(ctx->lib_task_done);
    vTaskDelete(nullptr);
}

extern "C" {

static error_t start_device(struct Device* device) {
    auto* cfg = GET_CONFIG(device);
    if (!cfg) {
        LOG_E(TAG, "device config is null");
        return ERROR_INVALID_ARGUMENT;
    }

    usb_host_config_t host_cfg = {
        .skip_phy_setup      = false,
        .root_port_unpowered = false,
        .intr_flags          = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb      = nullptr,
        .fifo_settings_custom = {},
#if CONFIG_IDF_TARGET_ESP32P4
        .peripheral_map      = cfg->peripheral_map,
#endif
    };

    esp_err_t ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK) {
        LOG_E(TAG, "usb_host_install failed: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    auto* ctx = new UsbHostContext();

    ctx->lib_task_done = xSemaphoreCreateBinary();
    if (!ctx->lib_task_done) {
        LOG_E(TAG, "failed to create lib_task_done semaphore");
        delete ctx;
        usb_host_uninstall();
        return ERROR_RESOURCE;
    }

    BaseType_t result = xTaskCreate(usbLibTask, "usb_lib", USB_LIB_TASK_STACK,
                                    ctx, USB_LIB_TASK_PRIORITY, &ctx->lib_task);
    if (result != pdPASS) {
        LOG_E(TAG, "failed to create usb_lib task");
        vSemaphoreDelete(ctx->lib_task_done);
        delete ctx;
        usb_host_uninstall();
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, ctx);
    LOG_I(TAG, "started (peripheral_map=0x%02x)", cfg->peripheral_map);
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(device));
    if (!ctx) return ERROR_NONE;

    xTaskNotifyGive(ctx->lib_task);

    bool exited = (xSemaphoreTake(ctx->lib_task_done, pdMS_TO_TICKS(USB_HOST_STOP_TIMEOUT_MS)) == pdTRUE);
    if (!exited) {
        // Free all devices to unblock the NO_CLIENTS / ALL_FREE flags, then retry.
        usb_host_device_free_all();
        exited = (xSemaphoreTake(ctx->lib_task_done, pdMS_TO_TICKS(USB_HOST_STOP_RETRY_MS)) == pdTRUE);
    }
    if (!exited) {
        LOG_E(TAG, "lib task stop timed out after %dms, force terminating — USB host restart required",
              USB_HOST_STOP_TIMEOUT_MS + USB_HOST_STOP_RETRY_MS);
        vTaskDelete(ctx->lib_task);
        vTaskDelay(pdMS_TO_TICKS(50));
        // Skip usb_host_uninstall — USB stack is in undefined state.
        ctx->lib_task = nullptr;
        vSemaphoreDelete(ctx->lib_task_done);
        device_set_driver_data(device, nullptr);
        delete ctx;
        return ERROR_RESOURCE;
    }
    ctx->lib_task = nullptr;
    vSemaphoreDelete(ctx->lib_task_done);

    esp_err_t uninstall_err = usb_host_uninstall();
    if (uninstall_err != ESP_OK) {
        LOG_W(TAG, "usb_host_uninstall: %s", esp_err_to_name(uninstall_err));
    }

    device_set_driver_data(device, nullptr);
    delete ctx;
    LOG_I(TAG, "stopped");
    return ERROR_NONE;
}

Driver esp32_usbhost_driver = {
    .name         = "esp32_usbhost",
    .compatible   = (const char*[]) { "espressif,esp32-usbhost", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = nullptr,
    .device_type  = nullptr,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED
