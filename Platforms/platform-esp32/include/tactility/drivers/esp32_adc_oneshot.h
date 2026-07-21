// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/device.h>

#include <esp_adc/adc_oneshot.h>
#include <hal/adc_types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32AdcOneshotChannelConfig {
    adc_channel_t channel;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
};

struct Esp32AdcOneshotConfig {
    adc_unit_t unit_id;
    adc_oneshot_clk_src_t clk_src;
    adc_ulp_mode_t ulp_mode;
    /** Per-channel configuration */
    const struct Esp32AdcOneshotChannelConfig* channels;
    /** The item count of channels */
    size_t channel_count;
};

#ifdef __cplusplus
}
#endif
