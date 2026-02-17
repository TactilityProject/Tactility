// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdint.h>
#include <tactility/error.h>
#include <tactility/freertos/freertos.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;

struct Pi4ioe5v6408Config {
    /** Address on bus */
    uint8_t address;
};

error_t pi4ioe5v6408_set_direction(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_set_output_level(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_get_output_level(struct Device* device, uint8_t* bits, TickType_t timeout);

error_t pi4ioe5v6408_set_output_high_impedance(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_set_input_default_level(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_set_pull_enable(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_set_pull_select(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_get_input_level(struct Device* device, uint8_t* bits, TickType_t timeout);

error_t pi4ioe5v6408_set_interrupt_mask(struct Device* device, uint8_t bits, TickType_t timeout);

error_t pi4ioe5v6408_get_interrupt_level(struct Device* device, uint8_t* bits, TickType_t timeout);

#ifdef __cplusplus
}
#endif
