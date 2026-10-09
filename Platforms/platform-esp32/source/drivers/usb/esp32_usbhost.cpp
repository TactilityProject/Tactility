#include <sdkconfig.h>
#ifdef CONFIG_SOC_USB_OTG_SUPPORTED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbhost.h>
#include <tactility/drivers/esp32_usbhost_task.h>
#include <tactility/drivers/usb_host.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <usb/usb_host.h>
#include <esp_intr_alloc.h>

#define TAG "esp32_usbhost"

#define GET_CONFIG(device) ((const Esp32UsbHostConfig*)(device)->config)

#define USB_HOST_AUDIO_SUPPORTED (CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S3)

extern "C" {
extern Driver esp32_usbhost_hid_driver;
extern Driver esp32_usbhost_midi_driver;
extern Driver esp32_usbhost_msc_driver;
#if USB_HOST_AUDIO_SUPPORTED
extern Driver esp32_usbhost_uac_driver;
#endif
}

constexpr auto USB_LIB_TASK_STACK        = 4096;
constexpr auto USB_LIB_TASK_PRIORITY     = 10;
constexpr auto USB_LIB_EVENT_TIMEOUT_MS  = 500;
constexpr auto USB_HOST_STOP_TIMEOUT_MS  = 3000;
constexpr auto USB_HOST_STOP_RETRY_MS    = 1000;
constexpr auto USB_HOST_CLASS_COUNT           = USB_HOST_CLASS_AUDIO + 1;
constexpr auto CHILD_DESTRUCT_RETRIES    = 100;

struct UsbHostChild {
    Device device = {};
    bool active = false;
};

struct UsbHostContext {
    TaskHandle_t      lib_task     = nullptr;
    SemaphoreHandle_t lib_task_done = nullptr;
    // Indexed by UsbClass
    UsbHostChild children[USB_HOST_CLASS_COUNT] = {};
};

// Stack may be in PSRAM: no flash access allowed from this task.
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
    vTaskDeleteWithCaps(nullptr);
}

// region Children

struct UsbHostChildSpec {
    const char* name;
    Driver* driver;
};

static UsbHostChildSpec get_child_spec(UsbClass usb_class) {
    switch (usb_class) {
        case USB_HOST_CLASS_HID:
            return { "usbhosthid0", &esp32_usbhost_hid_driver };
        case USB_HOST_CLASS_MIDI:
            return { "usbhostmidi0", &esp32_usbhost_midi_driver };
        case USB_HOST_CLASS_MSC:
            return { "usbhostmsc0", &esp32_usbhost_msc_driver };
        case USB_HOST_CLASS_AUDIO:
#if USB_HOST_AUDIO_SUPPORTED
            return { "usbhostaudio0", &esp32_usbhost_uac_driver };
#else
            return { "usbhostaudio0", nullptr };
#endif
    }
    return { nullptr, nullptr };
}

static bool is_class_configured(const Esp32UsbHostConfig* config, UsbClass usb_class) {
    switch (usb_class) {
        case USB_HOST_CLASS_HID: return config->hid;
        case USB_HOST_CLASS_MIDI: return config->midi;
        case USB_HOST_CLASS_MSC: return config->msc;
        case USB_HOST_CLASS_AUDIO: return config->audio;
    }
    return false;
}

static void create_child(Device* host, UsbHostChild* child, UsbClass usb_class) {
    const auto spec = get_child_spec(usb_class);
    if (spec.driver == nullptr) {
        LOG_W(TAG, "%s is not supported on this target", spec.name);
        return;
    }

    child->device = Device {
        .address = 0,
        .name = spec.name,
        .config = nullptr,
        .parent = nullptr,
        .internal = nullptr,
    };

    if (device_construct(&child->device) != ERROR_NONE) {
        LOG_E(TAG, "failed to construct %s", spec.name);
        return;
    }
    device_set_parent(&child->device, host);
    device_set_driver(&child->device, spec.driver);
    if (device_add(&child->device) != ERROR_NONE) {
        LOG_E(TAG, "failed to add %s", spec.name);
        device_destruct(&child->device);
        return;
    }
    if (device_start(&child->device) != ERROR_NONE) {
        LOG_E(TAG, "failed to start %s", spec.name);
        device_remove(&child->device);
        device_destruct(&child->device);
        return;
    }

    child->active = true;
}

static void destroy_child(UsbHostChild* child) {
    if (!child->active) {
        return;
    }
    child->active = false;

    device_stop(&child->device);
    device_remove(&child->device);
    // Short-lived device_get() references block destruction
    for (int i = 0; device_destruct(&child->device) == ERROR_RESOURCE_BUSY && i < CHILD_DESTRUCT_RETRIES; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// endregion

// region Lib

static error_t install_lib(UsbHostContext* ctx, const Esp32UsbHostConfig* cfg) {
    usb_host_config_t host_cfg = {
        .skip_phy_setup      = false,
        .root_port_unpowered = false,
        .intr_flags          = ESP_INTR_FLAG_LEVEL1,
        .enum_filter_cb      = nullptr,
        .fifo_settings_custom = {},
        .peripheral_map      = cfg->peripheral_map,
    };

    esp_err_t ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK) {
        LOG_E(TAG, "usb_host_install failed: %s", esp_err_to_name(ret));
        return ERROR_RESOURCE;
    }

    ctx->lib_task_done = xSemaphoreCreateBinary();
    if (!ctx->lib_task_done) {
        LOG_E(TAG, "failed to create lib_task_done semaphore");
        usb_host_uninstall();
        return ERROR_RESOURCE;
    }

    BaseType_t result = esp32_usbhost_task_create_psram(usbLibTask, "usb_lib", USB_LIB_TASK_STACK,
                                                        ctx, USB_LIB_TASK_PRIORITY, &ctx->lib_task);
    if (result != pdPASS) {
        LOG_E(TAG, "failed to create usb_lib task");
        vSemaphoreDelete(ctx->lib_task_done);
        ctx->lib_task_done = nullptr;
        usb_host_uninstall();
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t uninstall_lib(UsbHostContext* ctx) {
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
        vTaskDeleteWithCaps(ctx->lib_task);
        vTaskDelay(pdMS_TO_TICKS(50));
        // Skip usb_host_uninstall — USB stack is in undefined state.
        ctx->lib_task = nullptr;
        vSemaphoreDelete(ctx->lib_task_done);
        ctx->lib_task_done = nullptr;
        return ERROR_RESOURCE;
    }
    ctx->lib_task = nullptr;
    vSemaphoreDelete(ctx->lib_task_done);
    ctx->lib_task_done = nullptr;

    esp_err_t uninstall_err = usb_host_uninstall();
    if (uninstall_err != ESP_OK) {
        LOG_W(TAG, "usb_host_uninstall: %s", esp_err_to_name(uninstall_err));
    }
    return ERROR_NONE;
}

// endregion

// region API

static error_t is_class_enabled(struct Device* device, enum UsbClass usb_class, bool* enabled) {
    if (usb_class < 0 || usb_class >= USB_HOST_CLASS_COUNT) {
        return ERROR_INVALID_ARGUMENT;
    }
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(device));
    *enabled = ctx->children[usb_class].active;
    return ERROR_NONE;
}

// endregion

extern "C" {

static error_t start_device(struct Device* device) {
    auto* cfg = GET_CONFIG(device);
    if (!cfg) {
        LOG_E(TAG, "device config is null");
        return ERROR_INVALID_ARGUMENT;
    }

    auto* ctx = new UsbHostContext();
    if (install_lib(ctx, cfg) != ERROR_NONE) {
        delete ctx;
        return ERROR_RESOURCE;
    }
    device_set_driver_data(device, ctx);

    for (int i = 0; i < USB_HOST_CLASS_COUNT; i++) {
        const auto usb_class = static_cast<UsbClass>(i);
        if (is_class_configured(cfg, usb_class)) {
            create_child(device, &ctx->children[i], usb_class);
        }
    }

    LOG_I(TAG, "started (peripheral_map=0x%02x)", cfg->peripheral_map);
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(device));
    if (!ctx) return ERROR_NONE;

    for (int i = USB_HOST_CLASS_COUNT - 1; i >= 0; i--) {
        destroy_child(&ctx->children[i]);
    }

    error_t result = uninstall_lib(ctx);
    device_set_driver_data(device, nullptr);
    delete ctx;
    LOG_I(TAG, "stopped");
    return result;
}

static const UsbHostApi usb_host_api = {
    .is_class_enabled = is_class_enabled,
};

Driver esp32_usbhost_driver = {
    .name         = "esp32_usbhost",
    .compatible   = (const char*[]) { "espressif,esp32-usbhost", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .probe        = nullptr,
    .api          = &usb_host_api,
    .device_type  = &USB_HOST_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED
