#include "devices/Display.h"

#include <Tactility/hal/Configuration.h>
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/drivers/esp32_spi.h>

using namespace tt::hal;

static bool initBoot() {
    // The LoRa and SD chip-selects share the EPD's SPI bus but aren't wired up
    // yet. spi0's start() already drives every cs-gpio high during kernel init;
    // re-assert deselection before the EPD driver first transacts, as a cheap
    // guard against either chip latching stray EPD command bytes (same pattern
    // as the esp32_sdspi mount path).
    auto* spi0 = device_find_by_name("spi0");
    check(spi0 != nullptr);
    esp32_spi_deselect_all_cs(spi0);

    return true;
}

static DeviceVector createDevices() {
    // Touch, keyboard and battery/charging are kernel drivers (cst66xx, tca8418, sy6970,
    // bq27220 - see the .dts), started independently of this HAL layer. Only the EPD
    // display remains on the deprecated HAL.
    return DeviceVector {
        createDisplay()
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
