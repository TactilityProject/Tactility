#pragma once

struct FirmwareOps;

/** @return the esp_hosted co-processor FirmwareOps implementation. Only declared/linked when
 *  CONFIG_SLAVE_SOC_WIFI_SUPPORTED - callers (esp32_wifi.cpp) must guard with the same #if. */
const FirmwareOps* esp32_esp_hosted_ota_get_ops();
