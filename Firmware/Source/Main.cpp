#include <Tactility/Tactility.h>

#include <devicetree.h>

#ifdef ESP_PLATFORM
#include <tt_init.h>
#else
#include <Simulator.h>
#endif

extern "C" {

void app_main() {

#ifdef ESP_PLATFORM
    tt_init_tactility_c(); // ELF bindings for side-loading on ESP32
#endif

    tt::run(dts_modules, dts_devices);
}

} // extern