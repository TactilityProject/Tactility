#ifdef ESP_PLATFORM

#include "Tactility/hal/usb/UsbTusb.h"
#include <Tactility/PartitionsEsp.h>

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

bool tusbStartMassStorageWithFlash(bool fromBootMode) {
    LOG_I(TAG, "Starting flash MSC");

    auto* device = usb_msc_device_get();
    if (device == nullptr) {
        LOG_E(TAG, "USB MSC device not available");
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "USB MSC device not available");
        return false;
    }

    wl_handle_t handle = tt::getDataPartitionWlHandle();
    if (handle == WL_INVALID_HANDLE) {
        LOG_E(TAG, "WL not mounted for /data");
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "WL not mounted for /data");
        device_put(device);
        return false;
    }

    tt::hal::usb::startedFromBootMode = fromBootMode;

    auto result = usb_msc_device_start(device, USB_MSC_DEVICE_SOURCE_FLASH,
        &handle, tt::hal::usb::onMountChanged, nullptr);
    if (result != ERROR_NONE) {
        LOG_E(TAG, "TinyUSB flash init failed: %s", error_to_string(result));
        snprintf(tt::hal::usb::lastErrorMessage, sizeof(tt::hal::usb::lastErrorMessage), "Flash init failed: %s (%s)", error_to_string(result), usb_msc_device_get_last_error());
    } else {
        LOG_I(TAG, "TinyUSB flash init success");
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
        usb_msc_device_stop(device);
        device_put(device);
    }
}

bool tusbCanStartMassStorageWithFlash() {
    return tusbIsSupported() && (tt::getDataPartitionWlHandle() != WL_INVALID_HANDLE);
}

#else

bool tusbIsSupported() { return false; }
bool tusbStartMassStorageWithSdmmc(bool /*fromBootMode*/) { return false; }
bool tusbStartMassStorageWithFlash(bool /*fromBootMode*/) { return false; }
void tusbStop() {}
bool tusbCanStartMassStorageWithFlash() { return false; }
const char* tusbGetLastError() { return ""; }

#endif // CONFIG_TINYUSB_MSC_ENABLED

#endif // ESP_PLATFORM
