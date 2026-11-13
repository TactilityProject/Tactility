#pragma once

/** USB host on/off, persisted in the USB host settings. */
namespace tt::hal::usbhost {

/** Applies the saved setting to the USB host device. */
void systemStart();

/** @return true when the hardware has a USB host device */
bool isAvailable();

bool isEnabled();

/**
 * @return true when the host was enabled and failed to start. The choice is saved, so it is tried again after a reboot.
 * Cleared when the host is started or disabled.
 */
bool hasStartFailed();

/** Saves the choice, then starts or stops the USB host. */
bool setEnabled(bool enabled);

} // namespace tt::hal::usbhost
