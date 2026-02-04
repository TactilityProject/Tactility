// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/i2s_controller.h>
#include <driver/i2s.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32I2sConfig {
    i2s_port_t port;
    i2s_mode_t mode;aaa
    uint32_t sampleRate;
    i2s_bits_per_sample_t bitsPerSample;
    i2s_channel_fmt_t channelFormat;
    i2s_comm_format_t communicationFormat;
    int pinBclk;
    int pinWs;
    int pinDataOut;
    int pinDataIn;
    int pinMclk;
};

error_t esp32_i2s_get_port(struct Device* device, i2s_port_t* port);

#ifdef __cplusplus
}
#endif
