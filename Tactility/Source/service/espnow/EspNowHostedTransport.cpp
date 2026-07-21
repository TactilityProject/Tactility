#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <Tactility/service/espnow/EspNowHostedTransport.h>

#include <tactility/drivers/wifi.h>

namespace tt::service::espnow::backend {

// Thin wrapper: the actual CP_INIT wait/generation-tracking logic lives in the kernel driver
// (Platforms/platform-esp32/source/drivers/esp32_esp_hosted_ota.cpp, reached via the generic
// FirmwareOps::wait_ready() interface in tactility/drivers/wifi.h) so it's a single source of
// truth shared with any other caller (e.g. an external OTA app), instead of being duplicated
// here.
bool waitForHostedTransport(uint32_t timeoutMs) {
    Device* device = wifi_find_first_registered_device();
    if (device == nullptr) {
        return false;
    }
    const FirmwareOps* ops = nullptr;
    void* ctx = nullptr;
    if (wifi_get_firmware_ops(device, &ops, &ctx) != ERROR_NONE) {
        return false;
    }
    return ops->wait_ready(ctx, timeoutMs);
}

}

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
