#ifdef ESP_PLATFORM

#include "Tactility/hal/usb/UsbTusb.h"
#include <tactility/partitions_esp32.h>

#include <sdkconfig.h>

#if CONFIG_TINYUSB_MSC_ENABLED == 1

#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wear_levelling.h>

#include <cstdio>

#include <tactility/device.h>
#include <tactility/drivers/esp32_sdcard.h>
#include <tactility/drivers/usb_msc_device.h>
#include <tactility/log.h>

// Provided by Drivers/spi-nand-module when the board's user storage is a NAND block device.
// Weak so that boards without the module link and resolve to nullptr. The block-device type is
// only forward-declared here to avoid pulling esp_blockdev into the shared Tactility component.
struct esp_blockdev;
extern "C" struct esp_blockdev* spi_nand_get_user_storage_blockdev(void) __attribute__((weak));
extern "C" error_t spi_nand_suspend_user_storage(void) __attribute__((weak));
extern "C" error_t spi_nand_resume_user_storage(void) __attribute__((weak));
extern "C" error_t spi_nand_sync_user_storage(void) __attribute__((weak));

constexpr auto* TAG = "USB";

namespace tt::hal::usb {

// Set when mass storage was started as part of the dedicated reboot-into-MSC boot flow.
// Used to decide whether ejecting the volume should automatically reboot back to normal OS.
static bool startedFromBootMode = false;

static char lastErrorMessage[96] = "";

static sdmmc_card_t* getCard() {
    sdmmc_card_t* card = nullptr;

    device_for_each(&card, [](auto* device, void* context) {
        auto* driver = device_get_driver(device);
        if (driver == nullptr) return true;
        if (!driver_is_compatible(driver, "espressif,esp32-sdspi") &&
            !driver_is_compatible(driver, "espressif,esp32-sdmmc")) return true;
        auto** out = static_cast<sdmmc_card_t**>(context);
        *out = esp32_sdcard_get_card(device);
        return *out == nullptr;
    });

    if (card == nullptr) {
        LOG_W(TAG, "Couldn't find a mounted SD card");
    }

    return card;
}

static void onMountChanged(bool mounted, void* /*context*/) {
    if (mounted) {
        LOG_I(TAG, "MSC Mounted");
        // Storage is only (re)mounted into our own filesystem after the host sends a SCSI
        // START STOP UNIT eject (see tud_msc_start_stop_cb() in tusb_msc_storage.c). Windows
        // is known not to send this reliably, so this is a best-effort path for hosts that do
        // (e.g. Linux/macOS) - the "Return to OS" button on the boot screen is the primary one.
        // If we got here while booted into MSC mode, it's safe to reboot back into normal OS now.
        if (startedFromBootMode) {
            LOG_I(TAG, "MSC ejected by host, rebooting into normal OS");
            esp_restart();
        }
    } else {
        LOG_I(TAG, "MSC Unmounted");
    }
}

} // namespace tt::hal::usb

bool tusbIsSupported() { return true; }

bool tusbStartMassStorageWithSdmmc(bool fromBootMode) {
    auto* device = usb_msc_device_get();
    if (device == nullptr) {
        LOG_E(TAG, "USB MSC device not available");
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "USB MSC device not available");
        return false;
    }

    auto* card = tt::hal::usb::getCard();
    if (card == nullptr) {
        LOG_E(TAG, "SD card not mounted");
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "SD card not mounted");
        device_put(device);
        return false;
    }

    tt::hal::usb::startedFromBootMode = fromBootMode;

    auto result = usb_msc_device_start(device, USB_MSC_DEVICE_SOURCE_SDMMC, card,
        tt::hal::usb::onMountChanged, nullptr);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "TinyUSB SDMMC init failed: %s", error_to_string(result));
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "SDMMC init failed: %s (%s)", error_to_string(result), usb_msc_device_get_last_error());
    } else {
        LOG_I(TAG, "TinyUSB SDMMC init success");
    }

    device_put(device);
    return result == ERROR_NONE;
}

bool tusbStartMassStorageWithInternalStorage(bool fromBootMode) {
    LOG_I(TAG, "Starting internal-storage MSC");

    auto* device = usb_msc_device_get();
    if (device == nullptr) {
        LOG_E(TAG, "USB MSC device not available");
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "USB MSC device not available");
        return false;
    }

    wl_handle_t wl_handle = get_data_partition_wl_handle();
    struct esp_blockdev* blockdev = nullptr;
    if (wl_handle == WL_INVALID_HANDLE && spi_nand_get_user_storage_blockdev != nullptr) {
        blockdev = spi_nand_get_user_storage_blockdev();
    }

    if (wl_handle == WL_INVALID_HANDLE && blockdev == nullptr) {
        LOG_E(TAG, "No internal storage mounted");
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "No internal storage mounted");
        device_put(device);
        return false;
    }

    tt::hal::usb::startedFromBootMode = fromBootMode;

    error_t result;
    if (blockdev != nullptr) {
        // Hand the NAND volume to the host: release the local FAT mount first, as the USB
        // host and the OS must not write the same filesystem concurrently.
        if (spi_nand_suspend_user_storage != nullptr) {
            error_t suspend_error = spi_nand_suspend_user_storage();
            if (suspend_error != ERROR_NONE) {
                LOG_E(TAG, "Failed to unmount local NAND volume: %s", error_to_string(suspend_error));
                snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage),
                         "NAND unmount failed: %s", error_to_string(suspend_error));
                device_put(device);
                return false;
            }
        }
        result = usb_msc_device_start(device, USB_MSC_DEVICE_SOURCE_BLOCKDEV, &blockdev,
            tt::hal::usb::onMountChanged, nullptr);
    } else {
        result = usb_msc_device_start(device, USB_MSC_DEVICE_SOURCE_FLASH,
            &wl_handle, tt::hal::usb::onMountChanged, nullptr);
    }

    if (result != ERROR_NONE) {
        LOG_E(TAG, "TinyUSB MSC init failed: %s", error_to_string(result));
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "Storage init failed: %s (%s)", error_to_string(result), usb_msc_device_get_last_error());
        // The local volume was unmounted before the host took over. Put it back so the OS keeps
        // working if mass storage never started.
        if (blockdev != nullptr && spi_nand_resume_user_storage != nullptr) {
            error_t resume_error = spi_nand_resume_user_storage();
            if (resume_error != ERROR_NONE) {
                LOG_E(TAG, "Failed to remount the local NAND volume: %s", error_to_string(resume_error));
            }
        }
    } else {
        LOG_I(TAG, "TinyUSB MSC init success");
    }
    device_put(device);
    return result == ERROR_NONE;
}

const char* tusbGetLastError() {
    return tt::hal::usb::lastErrorMessage;
}

void tusbStop() {
    auto* device = usb_msc_device_get();
    if (device != nullptr) {
        // Flush any host writes that are still in the NAND journal before the driver is torn
        // down and the system reboots.
        if (spi_nand_sync_user_storage != nullptr) {
            error_t sync_error = spi_nand_sync_user_storage();
            if (sync_error != ERROR_NONE) {
                LOG_W(TAG, "Failed to flush NAND writes: %s", error_to_string(sync_error));
            }
        }
        usb_msc_device_stop(device);
        device_put(device);
    }
}

bool tusbCanStartMassStorageWithInternalStorage() {
    if (!tusbIsSupported()) {
        return false;
    }
    if (get_data_partition_wl_handle() != WL_INVALID_HANDLE) {
        return true;
    }
    return spi_nand_get_user_storage_blockdev != nullptr &&
           spi_nand_get_user_storage_blockdev() != nullptr;
}

#else

bool tusbIsSupported() { return false; }
bool tusbStartMassStorageWithSdmmc(bool /*fromBootMode*/) { return false; }
bool tusbStartMassStorageWithInternalStorage(bool /*fromBootMode*/) { return false; }
void tusbStop() {}
bool tusbCanStartMassStorageWithInternalStorage() { return false; }
const char* tusbGetLastError() { return ""; }

#endif // CONFIG_TINYUSB_MSC_ENABLED

#endif // ESP_PLATFORM
