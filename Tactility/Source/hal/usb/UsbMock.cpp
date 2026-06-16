#ifndef ESP_PLATFORM

#include "Tactility/hal/usb/Usb.h"

namespace tt::hal::usb {

bool startMassStorageWithSdmmc(bool /*fromBootMode*/) { return false; }
void stop() {}
Mode getMode() { return Mode::Default; }
BootMode getUsbBootMode() { return BootMode::None; }
bool isSupported() { return false; }

bool canRebootIntoMassStorageSdmmc() { return false; }
void rebootIntoMassStorageSdmmc() {}
bool startMassStorageWithFlash(bool /*fromBootMode*/) { return false; }
bool canRebootIntoMassStorageFlash() { return false; }
void rebootIntoMassStorageFlash() {}
bool isUsbBootMode() { return false; }
void resetUsbBootMode() {}

}

#endif
