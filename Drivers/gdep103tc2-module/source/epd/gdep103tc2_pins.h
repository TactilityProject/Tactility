// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "gdep103tc2_panel.h"

#include <drivers/gdep103tc2.h>
#include <tactility/error.h>

#include <driver/gpio.h>

/** Native ESP32-S3 pins of the parallel bus and the row control lines */
struct Gdep103tc2Pins {
    gpio_num_t ckv = GPIO_NUM_NC;
    gpio_num_t spv = GPIO_NUM_NC;
    gpio_num_t le = GPIO_NUM_NC;
    gpio_num_t sph = GPIO_NUM_NC;
    gpio_num_t wr = GPIO_NUM_NC;
    gpio_num_t data[BUS_WIDTH];
};

/**
 * Resolves the pins of the config to native GPIO numbers
 * @retval ERROR_INVALID_ARGUMENT when a pin is missing
 */
error_t gdep103tc2_pins_resolve(const Gdep103tc2Config* config, Gdep103tc2Pins* pins);
