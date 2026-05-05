#ifdef ESP_PLATFORM

#include <Tactility/Logger.h>
#include <Tactility/PartitionsEsp.h>
#include <Tactility/TactilityCore.h>

#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

namespace tt {

static auto LOGGER = Logger("Tactility");

// Initialize NVS
static void initNvs() {
    LOGGER.info("Init NVS");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        LOGGER.info("NVS erasing");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

static void initNetwork() {
    LOGGER.info("Init network");
    // Initialize network
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_init();
}

void initEsp() {
    initNvs();
    initPartitionsEsp();
    initNetwork();
}

} // namespace

#endif
