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

    extern Driver root_driver;
    extern Driver tlora_pager_driver;
    extern Driver esp32_gpio_driver;
    extern Driver esp32_i2c_driver;
    driver_construct(&root_driver);
    driver_construct(&tlora_pager_driver);
    driver_construct(&esp32_gpio_driver);
    driver_construct(&esp32_i2c_driver);

    devices_builtin_init();
    tt::run(config);
}

} // extern