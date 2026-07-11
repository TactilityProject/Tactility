// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct Device;

#define ADC_CHANNEL_SPEC_NONE ((struct AdcChannelSpec) { NULL, 0 })

/** The index of a channel on an ADC controller */
typedef uint8_t adc_channel_index_t;

/**
 * Specifies a channel on a specific ADC controller.
 * Used by the devicetree, drivers and application code to refer to ADC channels.
 */
struct AdcChannelSpec {
    /** ADC device controlling the channel */
    struct Device* adc_controller;
    /** The channel's index on the device */
    adc_channel_index_t channel;
};

#ifdef __cplusplus
}
#endif
