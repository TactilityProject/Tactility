#include "devices/Display.h"

#include <Tactility/hal/Configuration.h>

#include <tactility/drivers/i2c_controller.h>

using namespace tt::hal;

static bool init() {
    auto* i2c = device_find_by_name("i2c0");
    check(i2c);

    uint8_t write_buf = 0x01;
    i2c_controller_write(i2c, 0x24, &write_buf, 1, 200 / portTICK_PERIOD_MS);
    write_buf = 0x0E; // Pin 2, 3 and 4 high, pin 1 and 5 low
    i2c_controller_write(i2c, 0x38, &write_buf, 1, 200 / portTICK_PERIOD_MS);

    return true;
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = init,
    .createDevices = createDevices
};
