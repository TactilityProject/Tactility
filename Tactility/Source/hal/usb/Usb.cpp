#ifdef ESP_PLATFORM

#include <Tactility/hal/usb/Usb.h>
#include <Tactility/hal/sdcard/SpiSdCardDevice.h>
#include <Tactility/hal/usb/UsbTusb.h>

#include <Tactility/Logger.h>

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
    auto sdcards = findDevices<sdcard::SpiSdCardDevice>(Device::Type::SdCard);

    std::shared_ptr<sdcard::SpiSdCardDevice> usable_sdcard;
    for (auto& sdcard : sdcards) {
        auto sdcard_candidate = std::static_pointer_cast<sdcard::SpiSdCardDevice>(sdcard);
        if (sdcard_candidate != nullptr && sdcard_candidate->isMounted() && sdcard_candidate->getCard() != nullptr) {
            usable_sdcard = sdcard_candidate;
            break;
        }
    }

    if (usable_sdcard == nullptr) {
        LOGGER.warn("Couldn't find a mounted SpiSdCard");
        return nullptr;
    }

    auto* sdmmc_card = usable_sdcard->getCard();
    if (sdmmc_card == nullptr) {
        LOGGER.warn("SD card has no card object available");
        return nullptr;
    }

    return sdmmc_card;
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
