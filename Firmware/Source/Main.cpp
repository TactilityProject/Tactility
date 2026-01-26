#include <Tactility/Tactility.h>

#include <Tactility/Driver.h>
#include <devicetree.h>

#ifdef ESP_PLATFORM
#include <tt_init.h>
#else
#include <Simulator.h>
#endif

// Each board project declares this variable
extern const tt::hal::Configuration hardwareConfiguration;

extern "C" {

extern void register_kernel_drivers();
extern void register_platform_drivers();
extern void register_device_drivers();

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

    register_kernel_drivers();
    register_platform_drivers();
    register_device_drivers();

    devices_builtin_init();
    tt::run(config);
}

} // extern