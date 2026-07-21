#ifdef ESP_PLATFORM

#include <tactility/log.h>
#include <tactility/check.h>

#include <Tactility/PartitionsEsp.h>

#include "esp_event.h"
#include "esp_netif.h"

constexpr auto* TAG = "Tactility";

namespace tt {

static void initNetwork() {
    LOG_I(TAG, "Init network");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

void initEsp() {
    check(initPartitionsEsp(), "Failed to init partitions");
    initNetwork();
}

} // namespace

#endif
