#pragma once

namespace tt::lvgl {

#ifdef ESP_PLATFORM
/** Creates the USB keyboard and mouse indevs. Must be called from LvglModuleConfig.on_start. */
void startUsbHidInput();
/** Deletes the indevs created by startUsbHidInput(). Must be called from LvglModuleConfig.on_stop, before LVGL detaches its devices. */
void stopUsbHidInput();
#else
inline void startUsbHidInput() {}
inline void stopUsbHidInput() {}
#endif

} // namespace tt::lvgl
