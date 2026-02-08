#include "devices/Display.h"
#include "devices/SdCard.h"
#include <driver/gpio.h>

#include <Tactility/hal/Configuration.h>
#include <TCA9534.h>

using namespace tt::hal;

static bool initBoot() {
    TCA9534_IO_EXP io_expander = {
        .I2C_ADDR = 0x18,
        .i2c_master_port = I2C_NUM_0,
        .interrupt_pin = GPIO_NUM_NC,
        .interrupt_task = nullptr
    };

    // Enable LCD backlight
    set_tca9534_io_pin_direction(&io_expander, TCA9534_IO1, TCA9534_OUTPUT);
    set_tca9534_io_pin_output_state(&io_expander, TCA9534_IO1, 255);

    return true;
}

static DeviceVector createDevices() {
    return {
        createDisplay(),
        createSdCard()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
