#ifdef ESP_PLATFORM

#include <soc/soc_caps.h>

#include <Tactility/hal/usb/Usb.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>
#include <Tactility/hal/usb/UsbTusb.h>

#include <Tactility/Logger.h>
#include <tactility/drivers/esp32_sdmmc.h>

namespace tt::hal::usb {

static const auto LOGGER = Logger("USB");

constexpr auto BOOT_FLAG_SDMMC = 42;  // Existing
constexpr auto BOOT_FLAG_FLASH = 43;  // For flash mode

struct BootModeData {
    uint32_t flag = 0;
};

static Mode currentMode = Mode::Default;
static RTC_NOINIT_ATTR BootModeData bootModeData;

sdmmc_card_t* getCard() {
    sdmmc_card_t* sdcard = nullptr;

    // Find old HAL SD card device:
    auto sdcards = findDevices<sdcard::SpiSdCardDevice>(Device::Type::SdCard);
    for (auto& device : sdcards) {
        auto sdcard_device= std::static_pointer_cast<sdcard::SpiSdCardDevice>(device);
        if (sdcard_device != nullptr && sdcard_device->isMounted() && sdcard_device->getCard() != nullptr) {
            sdcard = sdcard_device->getCard();
            break;
        }
    }

#if SOC_SDMMC_HOST_SUPPORTED
    // Find ESP32 SDMMC device:
    if (sdcard == nullptr) {
        device_for_each(&sdcard, [](auto* device, void* context) {
            if (device_is_ready(device) && device_is_compatible(device, "espressif,esp32-sdmmc")) {
                auto** sdcard = static_cast<sdmmc_card_t**>(context);
                auto* sdmmc_card = esp32_sdmmc_get_card(device);
                if (sdmmc_card) {
                    *sdcard = sdmmc_card;
                    return false;
                }
                return true;
            }
            return true;
        });
    }
#endif

    if (sdcard == nullptr) {
        LOGGER.warn("Couldn't find a mounted SD card");
    }

    return sdcard;
}

static bool canStartNewMode() {
    return isSupported() && (currentMode == Mode::Default || currentMode == Mode::None);
}

bool isSupported() {
    return tusbIsSupported();
}

bool startMassStorageWithSdmmc() {
    if (!canStartNewMode()) {
        LOGGER.error("Can't start");
        return false;
    }

    if (tusbStartMassStorageWithSdmmc()) {
        currentMode = Mode::MassStorageSdmmc;
        return true;
    } else {
        LOGGER.error("Failed to init mass storage");
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
bool startMassStorageWithFlash() {
    if (!canStartNewMode()) {
        LOGGER.error("Can't start flash mass storage");
        return false;
    }

    if (tusbStartMassStorageWithFlash()) {
        currentMode = Mode::MassStorageFlash;
        return true;
    } else {
        LOGGER.error("Failed to init flash mass storage");
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
