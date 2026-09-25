// SPDX-License-Identifier: Apache-2.0
#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver esp32_dac_speaker_driver;

static Driver* const esp32_dac_speaker_drivers[] = {
    &esp32_dac_speaker_driver,
    nullptr
};

extern const ModuleSymbol esp32_dac_speaker_module_symbols[];

Module esp32_dac_speaker_module = {
    .name = "esp32_dac_speaker",
    .start = nullptr,
    .stop = nullptr,
    .drivers = esp32_dac_speaker_drivers,
    .symbols = esp32_dac_speaker_module_symbols,
    .internal = nullptr
};

}