#include <sdkconfig.h>
#ifdef CONFIG_SOC_USB_OTG_SUPPORTED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_usbhost.h>
#include <tactility/drivers/esp32_usbhost_backend.h>
#include <tactility/drivers/esp32_usbhost_task.h>
#include <tactility/drivers/esp32_usbhost_worker.h>
#include <tactility/drivers/usb_host.h>
#include <tactility/log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <usb/usb_host.h>
#include <esp_intr_alloc.h>

#define TAG "esp32_usbhost"

#define GET_CONFIG(device) ((const Esp32UsbHostConfig*)(device)->config)

#define USB_HOST_AUDIO_SUPPORTED (CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S3)

constexpr auto USB_LIB_TASK_STACK        = 4096;
constexpr auto USB_LIB_TASK_PRIORITY     = 10;
constexpr auto USB_LIB_EVENT_TIMEOUT_MS  = 500;
constexpr auto USB_HOST_STOP_TIMEOUT_MS  = 3000;
constexpr auto USB_HOST_STOP_RETRY_MS    = 1000;
constexpr auto USB_WORKER_TASK_STACK     = 4096;
constexpr auto USB_WORKER_TASK_PRIORITY  = 5;
constexpr auto USB_WORKER_QUEUE_SIZE     = 4;
constexpr auto USB_WORKER_IDLE_TIMEOUT_MS = 1000;
constexpr auto USB_HOST_CLASS_COUNT           = USB_HOST_CLASS_AUDIO + 1;
constexpr auto DEVICE_DESTRUCT_RETRIES   = 100;

struct UsbHostWorkerJob {
    // nullptr stops the worker
    void (*function)(void* context);
    void* context;
    StaticSemaphore_t done_buffer;
    SemaphoreHandle_t done;
};

struct UsbHostContext {
    // Exists only while there is work, guarded by worker_mutex
    TaskHandle_t      worker_task  = nullptr;
    QueueHandle_t     worker_queue = nullptr;
    SemaphoreHandle_t worker_mutex = nullptr;
    TaskHandle_t      lib_task     = nullptr;
    SemaphoreHandle_t lib_task_done = nullptr;
    // Backend contexts indexed by UsbClass, nullptr when not running
    void* backends[USB_HOST_CLASS_COUNT] = {};
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

// region Worker

// Runs device lifecycle work for the class drivers, so device listeners never run on a USB task.
// The stack stays in internal RAM because listeners may access flash.
// Created on demand and exits when idle, so it uses no memory while nothing is plugged in or out.
static void usbWorkerTask(void* arg) {
    auto* ctx = static_cast<UsbHostContext*>(arg);
    while (true) {
        UsbHostWorkerJob* job = nullptr;
        if (xQueueReceive(ctx->worker_queue, &job, pdMS_TO_TICKS(USB_WORKER_IDLE_TIMEOUT_MS)) != pdTRUE) {
            // Jobs are queued under worker_mutex, so none can arrive between this check and the exit
            xSemaphoreTake(ctx->worker_mutex, portMAX_DELAY);
            const bool idle = uxQueueMessagesWaiting(ctx->worker_queue) == 0;
            if (idle) {
                ctx->worker_task = nullptr;
            }
            xSemaphoreGive(ctx->worker_mutex);
            if (idle) {
                break;
            }
            continue;
        }
        if (job->function == nullptr) {
            xSemaphoreTake(ctx->worker_mutex, portMAX_DELAY);
            ctx->worker_task = nullptr;
            xSemaphoreGive(ctx->worker_mutex);
            xSemaphoreGive(job->done);
            break;
        }
        job->function(job->context);
        xSemaphoreGive(job->done);
    }
    vTaskDelete(nullptr);
}

static void wait_for_job(UsbHostWorkerJob* job) {
    xSemaphoreTake(job->done, portMAX_DELAY);
    vSemaphoreDelete(job->done);
}

static error_t create_worker_queue(UsbHostContext* ctx) {
    ctx->worker_mutex = xSemaphoreCreateMutex();
    ctx->worker_queue = xQueueCreate(USB_WORKER_QUEUE_SIZE, sizeof(UsbHostWorkerJob*));
    if (ctx->worker_mutex == nullptr || ctx->worker_queue == nullptr) {
        LOG_E(TAG, "failed to create worker queue");
        if (ctx->worker_mutex != nullptr) vSemaphoreDelete(ctx->worker_mutex);
        if (ctx->worker_queue != nullptr) vQueueDelete(ctx->worker_queue);
        ctx->worker_mutex = nullptr;
        ctx->worker_queue = nullptr;
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

// No jobs may be submitted anymore
static void delete_worker_queue(UsbHostContext* ctx) {
    UsbHostWorkerJob job = { .function = nullptr, .context = nullptr, .done_buffer = {}, .done = nullptr };
    job.done = xSemaphoreCreateBinaryStatic(&job.done_buffer);
    UsbHostWorkerJob* job_pointer = &job;

    xSemaphoreTake(ctx->worker_mutex, portMAX_DELAY);
    const bool running = ctx->worker_task != nullptr;
    if (running) {
        xQueueSend(ctx->worker_queue, &job_pointer, portMAX_DELAY);
    }
    xSemaphoreGive(ctx->worker_mutex);

    if (running) {
        wait_for_job(&job);
    } else {
        vSemaphoreDelete(job.done);
    }
    vQueueDelete(ctx->worker_queue);
    vSemaphoreDelete(ctx->worker_mutex);
    ctx->worker_queue = nullptr;
    ctx->worker_mutex = nullptr;
}

error_t esp32_usbhost_run_on_worker(struct Device* host, void (*function)(void* context), void* context) {
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(host));
    if (ctx == nullptr || ctx->worker_queue == nullptr) {
        return ERROR_INVALID_STATE;
    }

    UsbHostWorkerJob job = { .function = function, .context = context, .done_buffer = {}, .done = nullptr };
    UsbHostWorkerJob* job_pointer = &job;

    xSemaphoreTake(ctx->worker_mutex, portMAX_DELAY);
    if (ctx->worker_task != nullptr && xTaskGetCurrentTaskHandle() == ctx->worker_task) {
        xSemaphoreGive(ctx->worker_mutex);
        function(context);
        return ERROR_NONE;
    }
    if (ctx->worker_task == nullptr
        && xTaskCreate(usbWorkerTask, "usb_worker", USB_WORKER_TASK_STACK, ctx, USB_WORKER_TASK_PRIORITY, &ctx->worker_task) != pdPASS) {
        ctx->worker_task = nullptr;
        xSemaphoreGive(ctx->worker_mutex);
        LOG_E(TAG, "failed to create worker task");
        return ERROR_RESOURCE;
    }
    job.done = xSemaphoreCreateBinaryStatic(&job.done_buffer);
    xQueueSend(ctx->worker_queue, &job_pointer, portMAX_DELAY);
    xSemaphoreGive(ctx->worker_mutex);

    wait_for_job(&job);
    return ERROR_NONE;
}

// endregion

// region Devices

struct DeviceCreateJob {
    Device* device;
    Device* parent;
    const char* name;
    Driver* driver;
    void* driver_data;
    error_t result;
};

static void device_create_on_worker(void* context) {
    auto* job = static_cast<DeviceCreateJob*>(context);
    Device* device = job->device;
    *device = Device {};
    device->name = job->name;

    job->result = device_construct(device);
    if (job->result != ERROR_NONE) {
        LOG_E(TAG, "failed to construct %s", job->name);
        return;
    }
    device_set_driver_data(device, job->driver_data);
    device_set_parent(device, job->parent);
    device_set_driver(device, job->driver);
    job->result = device_add(device);
    if (job->result != ERROR_NONE) {
        LOG_E(TAG, "failed to add %s", job->name);
        device_destruct(device);
        return;
    }
    job->result = device_start(device);
    if (job->result != ERROR_NONE) {
        LOG_E(TAG, "failed to start %s", job->name);
        device_remove(device);
        device_destruct(device);
    }
}

struct DeviceDestroyJob {
    Device* device;
    error_t result;
};

static void device_destroy_on_worker(void* context) {
    auto* job = static_cast<DeviceDestroyJob*>(context);
    Device* device = job->device;

    if (device_is_added(device)) {
        error_t error = device_stop(device);
        if (error == ERROR_NONE) {
            error = device_remove(device);
        }
        if (error != ERROR_NONE) {
            LOG_E(TAG, "failed to stop %s: %s", device->name, error_to_string(error));
            job->result = error;
            return;
        }
    }

    // Short-lived device_get() references block destruction
    error_t error = device_destruct(device);
    for (int i = 0; error == ERROR_RESOURCE_BUSY && i < DEVICE_DESTRUCT_RETRIES; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        error = device_destruct(device);
    }
    if (error != ERROR_NONE) {
        LOG_E(TAG, "failed to destruct %s: %s", device->name, error_to_string(error));
    }
    job->result = error;
}

error_t esp32_usbhost_device_create(Device* host, Device* device, Device* parent, const char* name, Driver* driver, void* driver_data) {
    DeviceCreateJob job = { device, parent, name, driver, driver_data, ERROR_NONE };
    error_t error = esp32_usbhost_run_on_worker(host, device_create_on_worker, &job);
    return error != ERROR_NONE ? error : job.result;
}

error_t esp32_usbhost_device_destroy(Device* host, Device* device) {
    DeviceDestroyJob job = { device, ERROR_NONE };
    error_t error = esp32_usbhost_run_on_worker(host, device_destroy_on_worker, &job);
    return error != ERROR_NONE ? error : job.result;
}

// endregion

// region Backends

static const Esp32UsbHostBackend* get_backend(UsbClass usb_class) {
    switch (usb_class) {
        case USB_HOST_CLASS_HID: return &esp32_usbhost_hid_backend;
        case USB_HOST_CLASS_MIDI: return &esp32_usbhost_midi_backend;
        case USB_HOST_CLASS_MSC: return &esp32_usbhost_msc_backend;
        case USB_HOST_CLASS_AUDIO:
#if USB_HOST_AUDIO_SUPPORTED
            return &esp32_usbhost_uac_backend;
#else
            return nullptr;
#endif
    }
    return nullptr;
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

// Serializes backend changes with host start and stop, which create and delete the context
static SemaphoreHandle_t get_backends_mutex() {
    static StaticSemaphore_t buffer;
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutexStatic(&buffer);
    return mutex;
}

static bool is_class_supported_by(const Esp32UsbHostConfig* config, UsbClass usb_class) {
    return is_class_configured(config, usb_class) && get_backend(usb_class) != nullptr;
}

static error_t start_backend(Device* device, UsbHostContext* ctx, UsbClass usb_class) {
    error_t error = get_backend(usb_class)->start(device, &ctx->backends[usb_class]);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "failed to start class %d", usb_class);
        ctx->backends[usb_class] = nullptr;
    }
    return error;
}

static error_t stop_backend(UsbHostContext* ctx, UsbClass usb_class) {
    // A retained backend still uses the lib and the worker
    error_t error = get_backend(usb_class)->stop(ctx->backends[usb_class]);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "failed to stop class %d: %s", usb_class, error_to_string(error));
        return error;
    }
    ctx->backends[usb_class] = nullptr;
    return ERROR_NONE;
}

static error_t is_class_supported(struct Device* device, enum UsbClass usb_class, bool* supported) {
    if (usb_class < 0 || usb_class >= USB_HOST_CLASS_COUNT) {
        return ERROR_INVALID_ARGUMENT;
    }
    *supported = is_class_supported_by(GET_CONFIG(device), usb_class);
    return ERROR_NONE;
}

static error_t is_class_enabled(struct Device* device, enum UsbClass usb_class, bool* enabled) {
    if (usb_class < 0 || usb_class >= USB_HOST_CLASS_COUNT) {
        return ERROR_INVALID_ARGUMENT;
    }
    xSemaphoreTake(get_backends_mutex(), portMAX_DELAY);
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(device));
    if (ctx != nullptr) {
        *enabled = ctx->backends[usb_class] != nullptr;
    }
    xSemaphoreGive(get_backends_mutex());
    return ctx != nullptr ? ERROR_NONE : ERROR_INVALID_STATE;
}

static error_t set_class_enabled(struct Device* device, enum UsbClass usb_class, bool enabled) {
    if (usb_class < 0 || usb_class >= USB_HOST_CLASS_COUNT) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (!is_class_supported_by(GET_CONFIG(device), usb_class)) {
        return ERROR_NOT_SUPPORTED;
    }
    xSemaphoreTake(get_backends_mutex(), portMAX_DELAY);
    error_t error = ERROR_NONE;
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(device));
    if (ctx == nullptr) {
        error = ERROR_INVALID_STATE;
    } else if (enabled && ctx->backends[usb_class] == nullptr) {
        error = start_backend(device, ctx, usb_class);
    } else if (!enabled && ctx->backends[usb_class] != nullptr) {
        error = stop_backend(ctx, usb_class);
    }
    xSemaphoreGive(get_backends_mutex());
    return error;
}

// endregion

extern "C" {

static error_t start_device(struct Device* device) {
    auto* cfg = GET_CONFIG(device);
    if (!cfg) {
        LOG_E(TAG, "device config is null");
        return ERROR_INVALID_ARGUMENT;
    }

    xSemaphoreTake(get_backends_mutex(), portMAX_DELAY);
    auto* ctx = new UsbHostContext();
    if (create_worker_queue(ctx) != ERROR_NONE) {
        xSemaphoreGive(get_backends_mutex());
        delete ctx;
        return ERROR_RESOURCE;
    }
    if (install_lib(ctx, cfg) != ERROR_NONE) {
        xSemaphoreGive(get_backends_mutex());
        delete_worker_queue(ctx);
        delete ctx;
        return ERROR_RESOURCE;
    }
    device_set_driver_data(device, ctx);

    for (int i = 0; i < USB_HOST_CLASS_COUNT; i++) {
        const auto usb_class = static_cast<UsbClass>(i);
        if (is_class_supported_by(cfg, usb_class)) {
            start_backend(device, ctx, usb_class);
        }
    }
    xSemaphoreGive(get_backends_mutex());

    LOG_I(TAG, "started (peripheral_map=0x%02x)", cfg->peripheral_map);
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    xSemaphoreTake(get_backends_mutex(), portMAX_DELAY);
    auto* ctx = static_cast<UsbHostContext*>(device_get_driver_data(device));
    if (!ctx) {
        xSemaphoreGive(get_backends_mutex());
        return ERROR_NONE;
    }

    for (int i = USB_HOST_CLASS_COUNT - 1; i >= 0; i--) {
        if (ctx->backends[i] == nullptr) {
            continue;
        }
        error_t error = stop_backend(ctx, static_cast<UsbClass>(i));
        if (error != ERROR_NONE) {
            xSemaphoreGive(get_backends_mutex());
            return error;
        }
    }
    device_set_driver_data(device, nullptr);
    xSemaphoreGive(get_backends_mutex());

    error_t result = uninstall_lib(ctx);
    delete_worker_queue(ctx);
    delete ctx;
    LOG_I(TAG, "stopped");
    return result;
}

static const UsbHostApi usb_host_api = {
    .is_class_supported = is_class_supported,
    .is_class_enabled = is_class_enabled,
    .set_class_enabled = set_class_enabled,
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
