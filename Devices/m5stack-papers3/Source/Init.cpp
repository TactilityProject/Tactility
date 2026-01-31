#include "Tactility/kernel/SystemEvents.h"

#include <Tactility/TactilityCore.h>

#define TAG "papers3"

static bool powerOn() {
    // No power on sequence needed for M5Stack PaperS3
    return true;
}

bool initBoot() {
    ESP_LOGI(TAG, LOG_MESSAGE_POWER_ON_START);
    if (!powerOn()) {
        TT_LOG_E(TAG, LOG_MESSAGE_POWER_ON_FAILED);
        return false;
    }

    return true;
}
