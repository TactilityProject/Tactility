// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver spi_nand_driver;

static Driver* const spi_nand_drivers[] = {
    &spi_nand_driver,
    nullptr
};

Module spi_nand_module = {
    .name = "spi_nand",
    .start = nullptr,
    .stop = nullptr,
    .drivers = spi_nand_drivers,
    .symbols = nullptr,
    .internal = nullptr
};

} // extern "C"
