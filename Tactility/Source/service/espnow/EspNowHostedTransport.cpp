#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNowHostedTransport.h>

#include <tactility/drivers/esp_hosted_ota.h>

namespace tt::service::espnow::backend {

// Thin wrapper: the actual CP_INIT wait/generation-tracking logic lives in the kernel driver
// (Platforms/platform-esp32/source/drivers/esp32_esp_hosted_ota.cpp) so it's a single source of
// truth shared with any other caller of the kernel API (e.g. a future external OTA app), instead
// of being duplicated here.
bool waitForHostedTransport(uint32_t timeoutMs) {
    return esp_hosted_ota_wait_for_transport(timeoutMs);
}

}

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
