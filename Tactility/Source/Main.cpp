#include <Tactility/Tactility.h>
#include <Tactility/TactilityPrivate.h>

#include <devicetree.h>

#include <tactility/log.h>

#ifndef ESP_PLATFORM
#include <Simulator.h>
#endif

constexpr auto* TAG = "Main";

extern "C" {

void app_main() {
    if (!tt::init(dts_modules, dts_devices)) {
        return;
    }

    LOG_I(TAG, "Main dispatcher ready");
    const auto dispatcher = tt::getMainDispatcherHandle();
    while (true) {
        dispatcher_consume(dispatcher);
    }
}

} // extern
