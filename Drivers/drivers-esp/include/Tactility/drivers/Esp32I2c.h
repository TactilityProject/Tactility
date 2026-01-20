#pragma once

#include <Tactility/drivers/Gpio.h>
#include <hal/i2c_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2cConfig {
    uint32_t clock_frequency;
    struct GpioPinConfig pin_sda;
    struct GpioPinConfig pin_scl;
    const i2c_port_t port;
};

#ifdef __cplusplus
}
#endif
