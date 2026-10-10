#pragma once

/** USB host on/off, persisted in the USB host settings. */
namespace tt::hal::usbhost {

/** Applies the saved setting to the USB host device. */
void systemStart();

/** @return true when the hardware has a USB host device */
bool isAvailable();

bool isEnabled();

/** Starts or stops the USB host and saves the choice. */
bool setEnabled(bool enabled);

} // namespace tt::hal::usbhost
