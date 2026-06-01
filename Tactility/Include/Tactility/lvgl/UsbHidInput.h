#pragma once

namespace tt::lvgl {

#ifdef ESP_PLATFORM
void startUsbHidInput();
void stopUsbHidInput();
#else
inline void startUsbHidInput() {}
inline void stopUsbHidInput() {}
#endif

} // namespace tt::lvgl
