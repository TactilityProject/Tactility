#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>

#include <lilygo/drivers/tdeck_power_on.h>

constexpr auto* TAG = "tdeck-plus";

extern "C" {

static error_t start() {
    LOG_I(TAG, "Power on start");

    if (!tdeck_power_on()) {
        LOG_E(TAG, "Power on failed");
        return ERROR_RESOURCE;
    }

    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

Module lilygo_tdeck_plus_module = {
    .name = "lilygo-tdeck-plus",
    .start = start,
    .stop = stop,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
