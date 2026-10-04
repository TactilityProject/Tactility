#ifdef ESP_PLATFORM

#include <tactility/log.h>
#include <tactility/check.h>

#include "esp_event.h"
#include "esp_netif.h"

constexpr auto* TAG = "Tactility";

namespace tt {

void initEsp() {
    LOG_I(TAG, "Init esp_netif");
    ESP_ERROR_CHECK(esp_netif_init());
    LOG_I(TAG, "Init esp_event_loop");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

} // namespace

#endif
