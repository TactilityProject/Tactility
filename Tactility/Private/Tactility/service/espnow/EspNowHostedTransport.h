#pragma once

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#if defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)

#include <cstdint>

namespace tt::service::espnow::backend {

/**
 * The esp_hosted SDIO transport to the co-processor is brought up by esp_hosted_init()
 * (auto-called at process startup via a constructor attribute - always runs regardless of
 * WiFi/BT state), but the actual slave handshake only happens via esp_hosted_connect_to_slave(),
 * which today is only called as a side effect of the WiFi or BT stack starting
 * (esp32_wifi.cpp / vhci_drv.c). Nothing in Tactility's boot sequence calls it on its own, so
 * without this, ESP-NOW-over-bridge would require WiFi or BT to already be enabled - unlike
 * native ESP32 chips where ESP-NOW works standalone.
 *
 * This actively triggers the slave connect (safe to call even if already connected - the
 * underlying transport_drv_reconfigure() checks is_transport_tx_ready() first and returns
 * immediately if so) and then polls until the link is confirmed up or the timeout expires.
 *
 * @param timeoutMs how long to poll after triggering the connect before giving up
 * @return true once the transport is confirmed up, false on timeout
 */
bool waitForHostedTransport(uint32_t timeoutMs);

}

#endif // CONFIG_SLAVE_SOC_WIFI_SUPPORTED
