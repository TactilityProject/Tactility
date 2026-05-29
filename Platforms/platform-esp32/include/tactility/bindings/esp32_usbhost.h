// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <tactility/drivers/esp32_usbhost.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_usbhost, struct Esp32UsbHostConfig)
DEFINE_DEVICETREE(esp32_usbhost_hid, struct Esp32UsbHostChildConfig)
DEFINE_DEVICETREE(esp32_usbhost_midi, struct Esp32UsbHostChildConfig)
DEFINE_DEVICETREE(esp32_usbhost_msc, struct Esp32UsbHostChildConfig)

#ifdef __cplusplus
}
#endif
