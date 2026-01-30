#include <Tactility/Tactility.h>

#include <tactility/driver.h>
#include <devicetree.h>

// From the relevant platform
extern struct Module platform_module;
// From the relevant device
extern struct Module device_module;

#ifdef ESP_PLATFORM
#include <tt_init.h>
#else
#include <Simulator.h>
#endif

// Each board project declares this variable
extern const tt::hal::Configuration hardwareConfiguration;

extern "C" {

void app_main() {
    static const tt::Configuration config = {
        /**
         * Auto-select a board based on the ./sdkconfig.board.* file
         * that you copied to ./sdkconfig before you opened this project.
         */
        .hardware = &hardwareConfiguration
    };

#ifdef ESP_PLATFORM
    tt_init_tactility_c(); // ELF bindings for side-loading on ESP32
#endif

    tt::run(config, &platform_module, &device_module, devicetree_devices);
}

} // extern