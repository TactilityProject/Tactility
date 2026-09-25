// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/esp32_dac_speaker.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(esp32_dac_speaker, struct Esp32DacSpeakerConfig)

#ifdef __cplusplus
}
#endif