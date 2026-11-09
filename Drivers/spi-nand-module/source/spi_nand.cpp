// SPDX-License-Identifier: Apache-2.0
#include <drivers/spi_nand.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/sdcard.h>
#include <tactility/error.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/log.h>

#include <driver/gpio.h>
#include <driver/spi_common.h>
#include <driver/spi_master.h>
#include <esp_timer.h>
#include <esp_vfs_fat.h>
#include <spi_nand_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>
#include <new>

#define TAG "spi_nand"
#define GET_CONFIG(device) (static_cast<const SpiNandConfig*>((device)->config))

namespace {

struct SpiNandInternal {
    const SpiNandConfig* config;
    spi_device_handle_t spi_device;
    esp_blockdev_handle_t blockdev;
    FileSystem* file_system;
    GpioDescriptor* pin_wp;
    GpioDescriptor* pin_hd;
    bool mounted;

    // Block device handed to USB mass storage. It forwards to blockdev and syncs Dhara's
    // journal shortly after a burst of writes. Without this, small metadata updates (e.g. the
    // directory entry change for a host-side delete) stay in the journal until some later write
    // triggers a checkpoint, so they are lost on reboot while a large file copy still persists.
    esp_blockdev_t msc_device;
    SemaphoreHandle_t msc_mutex;
    esp_timer_handle_t msc_sync_timer;
    bool msc_dirty;
};

constexpr uint64_t MSC_SYNC_DELAY_US = 500 * 1000;

// Only one NAND volume exists per board. The USB mass-storage path looks it up through the
// getters below rather than walking the device tree.
SpiNandInternal* active_nand = nullptr;

// ---- USB mass-storage block device wrapper ----

SpiNandInternal* msc_owner(esp_blockdev_handle_t handle) {
    return static_cast<SpiNandInternal*>(handle->ctx);
}

esp_err_t msc_read(esp_blockdev_handle_t handle, uint8_t* dst_buf, size_t dst_buf_size,
                   uint64_t src_addr, size_t data_read_len) {
    auto* internal = msc_owner(handle);
    return internal->blockdev->ops->read(internal->blockdev, dst_buf, dst_buf_size, src_addr, data_read_len);
}

esp_err_t msc_write(esp_blockdev_handle_t handle, const uint8_t* src_buf, uint64_t dst_addr,
                    size_t data_write_len) {
    auto* internal = msc_owner(handle);
    xSemaphoreTake(internal->msc_mutex, portMAX_DELAY);
    esp_err_t error = internal->blockdev->ops->write(internal->blockdev, src_buf, dst_addr, data_write_len);
    if (error == ESP_OK) {
        internal->msc_dirty = true;
        // Restarting an already-armed one-shot fails. The pending timer then covers this write
        // too, which coalesces a burst into one sync.
        esp_timer_start_once(internal->msc_sync_timer, MSC_SYNC_DELAY_US);
    }
    xSemaphoreGive(internal->msc_mutex);
    return error;
}

esp_err_t msc_erase(esp_blockdev_handle_t handle, uint64_t start_addr, size_t erase_len) {
    auto* internal = msc_owner(handle);
    if (internal->blockdev->ops->erase == nullptr) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    xSemaphoreTake(internal->msc_mutex, portMAX_DELAY);
    esp_err_t error = internal->blockdev->ops->erase(internal->blockdev, start_addr, erase_len);
    if (error == ESP_OK) {
        internal->msc_dirty = true;
        esp_timer_start_once(internal->msc_sync_timer, MSC_SYNC_DELAY_US);
    }
    xSemaphoreGive(internal->msc_mutex);
    return error;
}

esp_err_t msc_sync(esp_blockdev_handle_t handle) {
    auto* internal = msc_owner(handle);
    xSemaphoreTake(internal->msc_mutex, portMAX_DELAY);
    esp_err_t error = ESP_OK;
    if (internal->msc_dirty) {
        error = internal->blockdev->ops->sync(internal->blockdev);
        if (error == ESP_OK) {
            internal->msc_dirty = false;
        }
    }
    xSemaphoreGive(internal->msc_mutex);
    return error;
}

esp_err_t msc_ioctl(esp_blockdev_handle_t handle, const uint8_t cmd, void* args) {
    auto* internal = msc_owner(handle);
    if (internal->blockdev->ops->ioctl == nullptr) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    return internal->blockdev->ops->ioctl(internal->blockdev, cmd, args);
}

// The wrapper borrows its source: the NAND driver owns and releases the real block device.
esp_err_t msc_release(esp_blockdev_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

const esp_blockdev_ops_t MSC_DEVICE_OPS = {
    .read = msc_read,
    .write = msc_write,
    .erase = msc_erase,
    .sync = msc_sync,
    .ioctl = msc_ioctl,
    .release = msc_release,
};

void msc_sync_timer_cb(void* arg) {
    auto* internal = static_cast<SpiNandInternal*>(arg);
    msc_sync(&internal->msc_device);
}

// WP# and HOLD# must be high for the chip to accept commands. Leave them out of the SPI
// peripheral's quad configuration and drive them as plain outputs instead.
error_t drive_pin_high(const GpioPinSpec& spec, GpioDescriptor** descriptor) {
    if (spec.gpio_controller == nullptr) {
        return ERROR_NONE;
    }
    // The devicetree spec carries no direction, so merge the output flag into the acquire
    // (acquire's own flag validation rejects an empty direction).
    *descriptor = gpio_descriptor_acquire(
        spec.gpio_controller,
        spec.pin,
        spec.flags | GPIO_FLAG_DIRECTION_OUTPUT,
        GPIO_OWNER_GPIO
    );
    if (*descriptor == nullptr) {
        return ERROR_RESOURCE;
    }
    if (gpio_descriptor_set_level(*descriptor, true) != ERROR_NONE) {
        gpio_descriptor_release(*descriptor);
        *descriptor = nullptr;
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

void release_control_pin(GpioDescriptor* descriptor) {
    if (descriptor != nullptr) {
        gpio_descriptor_release(descriptor);
    }
}

gpio_num_t to_native_pin(const GpioPinSpec& pin) {
    if (pin.gpio_controller == nullptr) {
        return GPIO_NUM_NC;
    }
    return static_cast<gpio_num_t>(pin.pin);
}

// region FileSystemApi

error_t fs_mount(void* data) {
    auto* internal = static_cast<SpiNandInternal*>(data);

    LOG_I(TAG, "Mounting %s", internal->config->mount_path);

    const esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = internal->config->format_if_mount_failed,
        .max_files = 5,
        // Cluster size for the (rare) format path. The FAT sector size comes from the NAND
        // integration. 16 KiB keeps the cluster count low on a 128 MB device.
        .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false,
        .use_one_fat = true,
    };

    esp_err_t result = esp_vfs_fat_bdl_mount(internal->config->mount_path, internal->blockdev, &mount_config);
    if (result != ESP_OK) {
        LOG_E(TAG, "Failed to mount %s: %s", internal->config->mount_path, esp_err_to_name(result));
        internal->mounted = false;
        return ERROR_RESOURCE;
    }

    uint64_t total = 0;
    uint64_t free = 0;
    if (esp_vfs_fat_info(internal->config->mount_path, &total, &free) == ESP_OK) {
        LOG_I(TAG, "Mounted %s: %llu KiB total, %llu KiB free", internal->config->mount_path,
              total / 1024, free / 1024);
    }

    internal->mounted = true;
    return ERROR_NONE;
}

error_t fs_unmount(void* data) {
    auto* internal = static_cast<SpiNandInternal*>(data);
    if (!internal->mounted) {
        return ERROR_NONE;
    }

    LOG_I(TAG, "Unmounting %s", internal->config->mount_path);
    if (esp_vfs_fat_bdl_unmount(internal->config->mount_path, internal->blockdev) != ESP_OK) {
        LOG_E(TAG, "Unmount failed for %s", internal->config->mount_path);
        return ERROR_UNDEFINED;
    }
    internal->mounted = false;
    return ERROR_NONE;
}

bool fs_is_mounted(void* data) {
    return static_cast<SpiNandInternal*>(data)->mounted;
}

error_t fs_get_path(void* data, char* out_path, size_t out_path_size) {
    const auto* internal = static_cast<SpiNandInternal*>(data);
    if (strlen(internal->config->mount_path) >= out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(out_path, internal->config->mount_path);
    return ERROR_NONE;
}

const FileSystemApi SPI_NAND_FS_API = {
    .mount = fs_mount,
    .unmount = fs_unmount,
    .is_mounted = fs_is_mounted,
    .get_path = fs_get_path,
};

// endregion

error_t start(Device* device) {
    const auto* config = GET_CONFIG(device);

    auto* internal = new (std::nothrow) SpiNandInternal {
        .config = config,
        .spi_device = nullptr,
        .blockdev = nullptr,
        .file_system = nullptr,
        .pin_wp = nullptr,
        .pin_hd = nullptr,
        .mounted = false,
    };
    if (internal == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }

    if (drive_pin_high(config->pin_wp, &internal->pin_wp) != ERROR_NONE ||
        drive_pin_high(config->pin_hd, &internal->pin_hd) != ERROR_NONE) {
        LOG_E(TAG, "Failed to drive WP#/HOLD# high");
        release_control_pin(internal->pin_wp);
        release_control_pin(internal->pin_hd);
        delete internal;
        return ERROR_RESOURCE;
    }

    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = to_native_pin(config->pin_mosi);
    bus_config.miso_io_num = to_native_pin(config->pin_miso);
    bus_config.sclk_io_num = to_native_pin(config->pin_clk);
    // WP#/HOLD# are driven high as GPIOs above, not by the SPI peripheral.
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;
    // A NAND page is 2 KiB for the supported chips. Leave room for cache maintenance.
    bus_config.max_transfer_sz = 4096 * 2;

    auto host = static_cast<spi_host_device_t>(config->host);
    esp_err_t result = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
    if (result != ESP_OK) {
        LOG_E(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(result));
        release_control_pin(internal->pin_wp);
        release_control_pin(internal->pin_hd);
        delete internal;
        return ERROR_RESOURCE;
    }

    spi_device_interface_config_t device_config = {};
    device_config.clock_speed_hz = static_cast<int>(config->max_freq_khz * 1000);
    device_config.mode = 0;
    device_config.spics_io_num = to_native_pin(config->pin_cs);
    device_config.queue_size = 10;
    device_config.flags = SPI_DEVICE_HALFDUPLEX;

    result = spi_bus_add_device(host, &device_config, &internal->spi_device);
    if (result != ESP_OK) {
        LOG_E(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(result));
        spi_bus_free(host);
        release_control_pin(internal->pin_wp);
        release_control_pin(internal->pin_hd);
        delete internal;
        return ERROR_RESOURCE;
    }

    spi_nand_flash_config_t nand_config = {};
    nand_config.device_handle = internal->spi_device;
    nand_config.gc_factor = config->gc_factor;
    nand_config.io_mode = SPI_NAND_IO_MODE_SIO;
    nand_config.flags = SPI_DEVICE_HALFDUPLEX;
    // S31: the interrupt/DMA path stalls on zero-length PAGE READ and GET FEATURE (STATUS)
    // transactions, so READY polling must use the polling transmit path.
    nand_config.poll_short_commands = true;

    result = spi_nand_flash_init_with_layers(&nand_config, &internal->blockdev);
    if (result != ESP_OK || internal->blockdev == nullptr) {
        LOG_E(TAG, "SPI NAND init failed: %s", esp_err_to_name(result));
        spi_bus_remove_device(internal->spi_device);
        spi_bus_free(host);
        release_control_pin(internal->pin_wp);
        release_control_pin(internal->pin_hd);
        delete internal;
        return ERROR_RESOURCE;
    }

    internal->msc_mutex = xSemaphoreCreateMutex();
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = msc_sync_timer_cb;
    timer_args.arg = internal;
    timer_args.name = "nand_msc_sync";
    if (internal->msc_mutex == nullptr || esp_timer_create(&timer_args, &internal->msc_sync_timer) != ESP_OK) {
        LOG_E(TAG, "Failed to create USB storage sync state");
        if (internal->msc_sync_timer != nullptr) {
            esp_timer_delete(internal->msc_sync_timer);
        }
        if (internal->msc_mutex != nullptr) {
            vSemaphoreDelete(internal->msc_mutex);
        }
        internal->blockdev->ops->release(internal->blockdev);
        spi_bus_remove_device(internal->spi_device);
        spi_bus_free(host);
        release_control_pin(internal->pin_wp);
        release_control_pin(internal->pin_hd);
        delete internal;
        return ERROR_RESOURCE;
    }

    internal->msc_device.ctx = internal;
    internal->msc_device.device_flags = internal->blockdev->device_flags;
    internal->msc_device.geometry = internal->blockdev->geometry;
    internal->msc_device.ops = &MSC_DEVICE_OPS;

    device_set_driver_data(device, internal);
    active_nand = internal;

    internal->file_system = file_system_add(config->mount_name, &SPI_NAND_FS_API, internal);
    if (internal->file_system == nullptr) {
        LOG_E(TAG, "Failed to register file system");
        active_nand = nullptr;
        esp_timer_delete(internal->msc_sync_timer);
        vSemaphoreDelete(internal->msc_mutex);
        internal->blockdev->ops->release(internal->blockdev);
        spi_bus_remove_device(internal->spi_device);
        spi_bus_free(host);
        release_control_pin(internal->pin_wp);
        release_control_pin(internal->pin_hd);
        device_set_driver_data(device, nullptr);
        delete internal;
        return ERROR_OUT_OF_MEMORY;
    }
    file_system_set_owner(internal->file_system, device);

    if (file_system_mount(internal->file_system) != ERROR_NONE) {
        // Not fatal: Tactility reports the missing volume and offers a reboot/power off, which
        // is also what happens when a removable card is absent. A format attempt (enabled via
        // devicetree) happens inside fs_mount(), so a factory-blank chip mounts on first boot.
        LOG_E(TAG, "File system did not mount");
    }

    return ERROR_NONE;
}

error_t stop(Device* device) {
    auto* internal = static_cast<SpiNandInternal*>(device_get_driver_data(device));
    if (internal == nullptr) {
        return ERROR_NONE;
    }

    if (active_nand == internal) {
        active_nand = nullptr;
    }

    if (internal->file_system != nullptr) {
        if (file_system_is_mounted(internal->file_system)) {
            file_system_unmount(internal->file_system);
        }
        file_system_remove(internal->file_system);
        internal->file_system = nullptr;
    }

    if (internal->msc_sync_timer != nullptr) {
        esp_timer_stop(internal->msc_sync_timer);
        esp_timer_delete(internal->msc_sync_timer);
        internal->msc_sync_timer = nullptr;
    }
    if (internal->msc_mutex != nullptr) {
        vSemaphoreDelete(internal->msc_mutex);
        internal->msc_mutex = nullptr;
    }

    auto host = static_cast<spi_host_device_t>(internal->config->host);
    if (internal->blockdev != nullptr) {
        internal->blockdev->ops->release(internal->blockdev);
        internal->blockdev = nullptr;
    }
    if (internal->spi_device != nullptr) {
        spi_bus_remove_device(internal->spi_device);
        internal->spi_device = nullptr;
    }
    spi_bus_free(host);
    release_control_pin(internal->pin_wp);
    release_control_pin(internal->pin_hd);

    device_set_driver_data(device, nullptr);
    delete internal;
    return ERROR_NONE;
}

} // namespace

extern "C" {

esp_blockdev_handle_t spi_nand_get_user_storage_blockdev(void) {
    return (active_nand != nullptr) ? &active_nand->msc_device : ESP_BLOCKDEV_HANDLE_INVALID;
}

error_t spi_nand_suspend_user_storage(void) {
    if (active_nand == nullptr || active_nand->file_system == nullptr) {
        return ERROR_NONE;
    }
    if (!file_system_is_mounted(active_nand->file_system)) {
        return ERROR_NONE;
    }
    return file_system_unmount(active_nand->file_system);
}

error_t spi_nand_resume_user_storage(void) {
    if (active_nand == nullptr || active_nand->file_system == nullptr) {
        return ERROR_INVALID_STATE;
    }
    if (file_system_is_mounted(active_nand->file_system)) {
        return ERROR_NONE;
    }
    return file_system_mount(active_nand->file_system);
}

error_t spi_nand_sync_user_storage(void) {
    if (active_nand == nullptr) {
        return ERROR_NONE;
    }
    return (active_nand->msc_device.ops->sync(&active_nand->msc_device) == ESP_OK) ? ERROR_NONE : ERROR_RESOURCE;
}

Driver spi_nand_driver = {
    .name = "spi_nand",
    .compatible = (const char*[]) { "espressif,spi-nand", nullptr },
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = &SDCARD_TYPE,
    .internal = nullptr
};

} // extern "C"
