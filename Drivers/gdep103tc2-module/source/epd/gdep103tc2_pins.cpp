// SPDX-License-Identifier: Apache-2.0
#include "gdep103tc2_pins.h"

#include <tactility/log.h>

#define TAG "Gdep103tc2Pins"

static_assert(GDEP103TC2_DATA_PIN_COUNT == BUS_WIDTH, "The config has one data pin per bus bit");

static bool resolve_pin(const GpioPinSpec& spec, const char* name, gpio_num_t* out) {
    if (spec.gpio_controller == nullptr) {
        LOG_E(TAG, "Pin %s is not set", name);
        return false;
    }
    *out = static_cast<gpio_num_t>(spec.pin);
    return true;
}

error_t gdep103tc2_pins_resolve(const Gdep103tc2Config* config, Gdep103tc2Pins* pins) {
    const GpioPinSpec* data_specs[BUS_WIDTH] = {
        &config->pin_data0, &config->pin_data1, &config->pin_data2, &config->pin_data3,
        &config->pin_data4, &config->pin_data5, &config->pin_data6, &config->pin_data7,
        &config->pin_data8, &config->pin_data9, &config->pin_data10, &config->pin_data11,
        &config->pin_data12, &config->pin_data13, &config->pin_data14, &config->pin_data15
    };

    bool ok = resolve_pin(config->pin_ckv, "ckv", &pins->ckv) &&
        resolve_pin(config->pin_spv, "spv", &pins->spv) &&
        resolve_pin(config->pin_le, "le", &pins->le) &&
        resolve_pin(config->pin_sph, "sph", &pins->sph) &&
        resolve_pin(config->pin_wr, "wr", &pins->wr);
    for (int i = 0; ok && i < BUS_WIDTH; i++) {
        ok = resolve_pin(*data_specs[i], "data", &pins->data[i]);
    }
    return ok ? ERROR_NONE : ERROR_INVALID_ARGUMENT;
}
