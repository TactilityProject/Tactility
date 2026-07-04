#include "PwmBacklight.h"

#include <Tactility/service/gps/GpsService.h>
#include <Tactility/TactilityCore.h>
#include <tactility/log.h>

constexpr auto* TAG = "T-Dongle S3";

bool initBoot() {
    if (!driver::pwmbacklight::init(GPIO_NUM_38, 12000)) {
        LOG_E(TAG, "Backlight init failed");
        return false;
    }

    return true;
}
