#include <sdkconfig.h>
#ifdef CONFIG_SOC_USB_OTG_SUPPORTED

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/usb_host_msc.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <atomic>
#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/portmacro.h>

#include <usb/msc_host.h>
#include <usb/msc_host_vfs.h>
#include <esp_vfs_fat.h>

#define TAG "esp32_usbhost_msc"

#define USB_MSC_MOUNT_PATH_PREFIX "/usb"

constexpr auto MAX_MSC_DEVICES      = 2;
constexpr auto MSC_EVENT_QUEUE_SIZE = 8;
constexpr auto MSC_PROC_TASK_STACK  = 4096;
constexpr auto MSC_PROC_TASK_PRIORITY = 5;
constexpr auto MSC_STOP_TIMEOUT_MS  = 2000;

typedef struct {
    uint8_t usb_addr;
    msc_host_device_handle_t device;
    msc_host_vfs_handle_t vfs;
    char mount_path[16];
    struct FileSystem* fs_entry;
    bool mounted;
} msc_dev_entry_t;

// The anonymous enum inside msc_host_event_t is C-only scoped; use a local alias in C++.
using msc_event_id_t = decltype(msc_host_event_t{}.event);
static constexpr msc_event_id_t kMscDeviceConnected    = static_cast<msc_event_id_t>(0);
static constexpr msc_event_id_t kMscDeviceDisconnected = static_cast<msc_event_id_t>(1);

enum class MscMsgId : uint8_t { Connected, Disconnected };

typedef struct {
    MscMsgId id;
    union {
        uint8_t address;
        msc_host_device_handle_t handle;
    };
} msc_msg_t;

struct UsbMscCtx {
    msc_dev_entry_t*  devs[MAX_MSC_DEVICES] = {};
    portMUX_TYPE      devs_lock             = portMUX_INITIALIZER_UNLOCKED;
    QueueHandle_t     event_queue           = nullptr;
    TaskHandle_t      proc_task             = nullptr;
    SemaphoreHandle_t proc_task_done        = nullptr;
    std::atomic<bool> proc_running{false};
};

static error_t usb_fs_mount(void* /*data*/) { return ERROR_NONE; }
static error_t usb_fs_unmount(void* /*data*/) { return ERROR_NONE; }
static bool    usb_fs_is_mounted(void* data) {
    if (!data) return false;
    return static_cast<msc_dev_entry_t*>(data)->mounted;
}
static error_t usb_fs_get_path(void* data, char* out_path, size_t out_path_size) {
    if (!data) return ERROR_INVALID_ARGUMENT;
    snprintf(out_path, out_path_size, "%s", static_cast<msc_dev_entry_t*>(data)->mount_path);
    return ERROR_NONE;
}

static const FileSystemApi usb_fs_api = {
    .mount      = usb_fs_mount,
    .unmount    = usb_fs_unmount,
    .is_mounted = usb_fs_is_mounted,
    .get_path   = usb_fs_get_path,
};

static int find_free_slot(UsbMscCtx* ctx) {
    for (int i = 0; i < MAX_MSC_DEVICES; i++) {
        if (ctx->devs[i] == nullptr) return i;
    }
    return -1;
}

static int find_slot_by_handle(UsbMscCtx* ctx, msc_host_device_handle_t handle) {
    for (int i = 0; i < MAX_MSC_DEVICES; i++) {
        if (ctx->devs[i] && ctx->devs[i]->device == handle) return i;
    }
    return -1;
}

static void free_msc_device(UsbMscCtx* ctx, int slot) {
    if (slot < 0 || slot >= MAX_MSC_DEVICES || !ctx->devs[slot]) return;
    if (ctx->devs[slot]->fs_entry) {
        ctx->devs[slot]->mounted = false;
        file_system_remove(ctx->devs[slot]->fs_entry);
        ctx->devs[slot]->fs_entry = nullptr;
    }
    if (ctx->devs[slot]->vfs) {
        msc_host_vfs_unregister(ctx->devs[slot]->vfs);
    }
    if (ctx->devs[slot]->device) {
        msc_host_uninstall_device(ctx->devs[slot]->device);
    }
    free(ctx->devs[slot]);
    ctx->devs[slot] = nullptr;
}

static void free_all_msc_devices(UsbMscCtx* ctx) {
    for (int i = 0; i < MAX_MSC_DEVICES; i++) {
        free_msc_device(ctx, i);
    }
}

static void msc_event_cb(const msc_host_event_t* event, void* arg) {
    auto* ctx = static_cast<UsbMscCtx*>(arg);
    if (!ctx->event_queue) return;
    msc_msg_t msg = {};
    if (event->event == kMscDeviceConnected) {
        msg.id      = MscMsgId::Connected;
        msg.address = event->device.address;
        if (xQueueSend(ctx->event_queue, &msg, 0) != pdTRUE) {
            LOG_W(TAG, "event queue full, dropped Connected event (addr=%d)", event->device.address);
        }
    } else if (event->event == kMscDeviceDisconnected) {
        msg.id     = MscMsgId::Disconnected;
        msg.handle = event->device.handle;
        if (xQueueSend(ctx->event_queue, &msg, 0) != pdTRUE) {
            LOG_W(TAG, "event queue full, dropped Disconnected event");
        }
    }
}

static void mscProcTask(void* arg) {
    auto* ctx = static_cast<UsbMscCtx*>(arg);
    LOG_I(TAG, "MSC proc task started");

    while (ctx->proc_running) {
        msc_msg_t msg;
        if (xQueueReceive(ctx->event_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) continue;

        if (msg.id == MscMsgId::Connected) {
            LOG_I(TAG, "USB drive connected (addr=%d)", msg.address);

            // Find a free slot under the lock, then allocate outside it.
            taskENTER_CRITICAL(&ctx->devs_lock);
            int slot = find_free_slot(ctx);
            taskEXIT_CRITICAL(&ctx->devs_lock);
            if (slot < 0) {
                LOG_W(TAG, "no free slots for USB drive");
                continue;
            }
            auto* entry = static_cast<msc_dev_entry_t*>(calloc(1, sizeof(msc_dev_entry_t)));
            if (!entry) {
                LOG_E(TAG, "failed to allocate MSC device entry");
                continue;
            }
            // Re-check the slot is still free before committing.
            taskENTER_CRITICAL(&ctx->devs_lock);
            if (ctx->devs[slot] != nullptr) {
                slot = find_free_slot(ctx);  // another path claimed it; find a new one
                if (slot >= 0) ctx->devs[slot] = entry;
            } else {
                ctx->devs[slot] = entry;
            }
            taskEXIT_CRITICAL(&ctx->devs_lock);
            if (slot < 0) {
                free(entry);
                LOG_W(TAG, "no free slots for USB drive after allocation");
                continue;
            }

            if (msc_host_install_device(msg.address, &ctx->devs[slot]->device) != ESP_OK) {
                LOG_E(TAG, "msc_host_install_device failed");
                taskENTER_CRITICAL(&ctx->devs_lock);
                free(ctx->devs[slot]);
                ctx->devs[slot] = nullptr;
                taskEXIT_CRITICAL(&ctx->devs_lock);
                continue;
            }
            ctx->devs[slot]->usb_addr = msg.address;
            snprintf(ctx->devs[slot]->mount_path, sizeof(ctx->devs[slot]->mount_path),
                     USB_MSC_MOUNT_PATH_PREFIX "%d", slot);
            const char* mount_path = ctx->devs[slot]->mount_path;

            const esp_vfs_fat_mount_config_t mount_cfg = {
                .format_if_mount_failed  = false,
                .max_files               = 4,
                .allocation_unit_size    = 4096,
                .disk_status_check_enable = false,
                .use_one_fat             = false,
            };
            esp_err_t vfs_err = msc_host_vfs_register(ctx->devs[slot]->device, mount_path, &mount_cfg, &ctx->devs[slot]->vfs);
            if (vfs_err != ESP_OK) {
                LOG_E(TAG, "msc_host_vfs_register failed for %s: %s", mount_path, esp_err_to_name(vfs_err));
                msc_host_uninstall_device(ctx->devs[slot]->device);
                taskENTER_CRITICAL(&ctx->devs_lock);
                free(ctx->devs[slot]);
                ctx->devs[slot] = nullptr;
                taskEXIT_CRITICAL(&ctx->devs_lock);
                continue;
            }
            LOG_I(TAG, "USB drive mounted at %s", mount_path);
            ctx->devs[slot]->mounted  = true;
            ctx->devs[slot]->fs_entry = file_system_add(&usb_fs_api, ctx->devs[slot]);
            if (!ctx->devs[slot]->fs_entry) {
                LOG_W(TAG, "failed to register filesystem for %s", mount_path);
            }

        } else if (msg.id == MscMsgId::Disconnected) {
            taskENTER_CRITICAL(&ctx->devs_lock);
            int slot = find_slot_by_handle(ctx, msg.handle);
            taskEXIT_CRITICAL(&ctx->devs_lock);
            if (slot >= 0) {
                LOG_I(TAG, "USB drive disconnected, unmounting slot %d", slot);
                free_msc_device(ctx, slot);
            }
        }
    }

    free_all_msc_devices(ctx);
    LOG_I(TAG, "MSC proc task stopped");
    xSemaphoreGive(ctx->proc_task_done);
    vTaskDelete(nullptr);
}

static bool api_eject(struct Device* device, const char* mount_path) {
    auto* ctx = static_cast<UsbMscCtx*>(device_get_driver_data(device));
    if (!ctx) return false;

    taskENTER_CRITICAL(&ctx->devs_lock);
    msc_dev_entry_t* entry = nullptr;
    int found = -1;
    for (int i = 0; i < MAX_MSC_DEVICES; i++) {
        if (ctx->devs[i] && strcmp(ctx->devs[i]->mount_path, mount_path) == 0) {
            found = i;
            entry = ctx->devs[i];
            ctx->devs[i] = nullptr;  // claim atomically under the lock
            break;
        }
    }
    taskEXIT_CRITICAL(&ctx->devs_lock);

    if (found >= 0) {
        LOG_I(TAG, "ejecting USB drive at %s (slot %d)", mount_path, found);
        // Free outside the lock; slot is already claimed (nullptr) so mscProcTask won't touch it.
        if (entry->fs_entry) {
            entry->mounted = false;
            file_system_remove(entry->fs_entry);
            entry->fs_entry = nullptr;
        }
        if (entry->vfs) {
            msc_host_vfs_unregister(entry->vfs);
        }
        if (entry->device) {
            msc_host_uninstall_device(entry->device);
        }
        free(entry);
        LOG_I(TAG, "USB drive ejected, safe to remove");
        return true;
    }
    LOG_W(TAG, "no drive mounted at %s", mount_path);
    return false;
}

static const UsbMscApi msc_api = {
    .eject = api_eject,
};

extern "C" {

static error_t start_device(struct Device* device) {
    auto* ctx = new UsbMscCtx();

    ctx->event_queue = xQueueCreate(MSC_EVENT_QUEUE_SIZE, sizeof(msc_msg_t));
    if (!ctx->event_queue) {
        LOG_E(TAG, "failed to create MSC event queue");
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->proc_task_done = xSemaphoreCreateBinary();
    if (!ctx->proc_task_done) {
        LOG_E(TAG, "failed to create task done semaphore");
        vQueueDelete(ctx->event_queue);
        delete ctx;
        return ERROR_RESOURCE;
    }

    const msc_host_driver_config_t msc_cfg = {
        .create_backround_task = true,
        .task_priority         = MSC_PROC_TASK_PRIORITY,
        .stack_size            = MSC_PROC_TASK_STACK,
        .core_id               = tskNO_AFFINITY,
        .callback              = msc_event_cb,
        .callback_arg          = ctx,
    };
    if (msc_host_install(&msc_cfg) != ESP_OK) {
        LOG_E(TAG, "msc_host_install failed");
        vQueueDelete(ctx->event_queue);
        vSemaphoreDelete(ctx->proc_task_done);
        delete ctx;
        return ERROR_RESOURCE;
    }

    ctx->proc_running = true;
    BaseType_t result = xTaskCreate(mscProcTask, "msc_proc", MSC_PROC_TASK_STACK,
                                    ctx, MSC_PROC_TASK_PRIORITY, &ctx->proc_task);
    if (result != pdPASS) {
        LOG_E(TAG, "failed to create msc_proc task");
        ctx->proc_running = false;
        msc_host_uninstall();
        vQueueDelete(ctx->event_queue);
        vSemaphoreDelete(ctx->proc_task_done);
        delete ctx;
        return ERROR_RESOURCE;
    }

    device_set_driver_data(device, ctx);
    LOG_I(TAG, "started");
    return ERROR_NONE;
}

static error_t stop_device(struct Device* device) {
    auto* ctx = static_cast<UsbMscCtx*>(device_get_driver_data(device));
    if (!ctx) return ERROR_NONE;

    ctx->proc_running = false;

    bool exited = (xSemaphoreTake(ctx->proc_task_done, pdMS_TO_TICKS(MSC_STOP_TIMEOUT_MS)) == pdTRUE);
    if (!exited) {
        // Retry once with a short extra wait before resorting to force-delete.
        exited = (xSemaphoreTake(ctx->proc_task_done, pdMS_TO_TICKS(500)) == pdTRUE);
    }
    if (!exited) {
        LOG_W(TAG, "MSC proc task stop timed out, force terminating");
        vTaskDelete(ctx->proc_task);
        vTaskDelay(pdMS_TO_TICKS(50));
        // Task was killed mid-cleanup — free devices ourselves as best-effort.
        free_all_msc_devices(ctx);
    }
    ctx->proc_task = nullptr;
    vSemaphoreDelete(ctx->proc_task_done);

    msc_host_uninstall();

    if (ctx->event_queue) { vQueueDelete(ctx->event_queue); ctx->event_queue = nullptr; }

    device_set_driver_data(device, nullptr);
    delete ctx;
    LOG_I(TAG, "stopped");
    return ERROR_NONE;
}

Driver esp32_usbhost_msc_driver = {
    .name         = "esp32_usbhost_msc",
    .compatible   = (const char*[]) { "espressif,esp32-usbhost-msc", nullptr },
    .start_device = start_device,
    .stop_device  = stop_device,
    .api          = &msc_api,
    .device_type  = &USB_HOST_MSC_TYPE,
    .owner        = nullptr,
    .internal     = nullptr,
};

} // extern "C"

#endif // CONFIG_SOC_USB_OTG_SUPPORTED
