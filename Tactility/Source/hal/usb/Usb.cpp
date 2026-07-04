#ifdef ESP_PLATFORM

#include <soc/soc_caps.h>

#include <Tactility/hal/usb/Usb.h>
#include <Tactility/hal/usb/UsbTusb.h>

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_sdcard.h>
#include <tactility/log.h>

namespace tt::hal::usb {

constexpr auto* TAG = "USB";

constexpr auto BOOT_FLAG_SDMMC = 42;  // Existing
constexpr auto BOOT_FLAG_FLASH = 43;  // For flash mode

struct BootModeData {
    uint32_t flag = 0;
};

static Mode currentMode = Mode::Default;
static RTC_NOINIT_ATTR BootModeData bootModeData;

sdmmc_card_t* getCard() {
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

static bool canStartNewMode() {
    return isSupported() && (currentMode == Mode::Default || currentMode == Mode::None);
}

bool isSupported() {
    return tusbIsSupported();
}

bool startMassStorageWithSdmmc(bool fromBootMode) {
    if (!canStartNewMode()) {
        LOG_E(TAG, "Can't start");
        return false;
    }

    if (tusbStartMassStorageWithSdmmc(fromBootMode)) {
        currentMode = Mode::MassStorageSdmmc;
        return true;
    } else {
        LOG_E(TAG, "Failed to init mass storage");
        return false;
    }
}

void stop() {
    if (canStartNewMode()) {
        return;
    }

    tusbStop();

    currentMode = Mode::None;
}

Mode getMode() {
    return currentMode;
}

bool canRebootIntoMassStorageSdmmc() {
    return tusbIsSupported() && getCard() != nullptr;
}

void rebootIntoMassStorageSdmmc() {
    if (tusbIsSupported()) {
        bootModeData.flag = BOOT_FLAG_SDMMC;
        esp_restart();
    }
}

// NEW: Flash mass storage functions
bool startMassStorageWithFlash(bool fromBootMode) {
    if (!canStartNewMode()) {
        LOG_E(TAG, "Can't start flash mass storage");
        return false;
    }

    if (tusbStartMassStorageWithFlash(fromBootMode)) {
        currentMode = Mode::MassStorageFlash;
        return true;
    } else {
        LOG_E(TAG, "Failed to init flash mass storage");
        return false;
    }
}

bool canRebootIntoMassStorageFlash() {
    return tusbCanStartMassStorageWithFlash();
}

void rebootIntoMassStorageFlash() {
    if (tusbCanStartMassStorageWithFlash()) {
        bootModeData.flag = BOOT_FLAG_FLASH;
        esp_restart();
    }
}

bool isUsbBootMode() {
    return bootModeData.flag == BOOT_FLAG_SDMMC || bootModeData.flag == BOOT_FLAG_FLASH;  // Support both
}

BootMode getUsbBootMode() {
    switch (bootModeData.flag) {
        case BOOT_FLAG_SDMMC:
            return BootMode::Sdmmc;
        case BOOT_FLAG_FLASH:
            return BootMode::Flash;
        default:
            return BootMode::None;
    }
}

void resetUsbBootMode() {
    bootModeData.flag = 0;
}

}

#endif
