// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver pdm_mic_driver;

static const Driver* pdm_mic_drivers[] = {
    &pdm_mic_driver,
    nullptr
};

extern const ModuleSymbol pdm_mic_module_symbols[];

Module pdm_mic_module = {
    .name = "pdm_mic",
    .drivers = pdm_mic_drivers,
    .symbols = pdm_mic_module_symbols,
};

}
