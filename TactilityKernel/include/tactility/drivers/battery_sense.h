// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/adc.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct BatterySenseConfig {
    /** The ADC channel connected to the battery sense circuit */
    struct AdcChannelSpec io_channel;
    /** The ADC's effective full-scale reference voltage at the channel's configured attenuation, in mV */
    uint32_t reference_voltage_mv;
    /** Voltage divider correction factor, fixed-point with 3 decimals (1000 = 1.000, i.e. no division) */
    uint32_t multiplier;
};

#ifdef __cplusplus
}
#endif
