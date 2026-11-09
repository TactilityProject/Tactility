// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bindings/bindings.h>
#include <drivers/spi_nand.h>

#ifdef __cplusplus
extern "C" {
#endif

DEFINE_DEVICETREE(spi_nand, struct SpiNandConfig)

#ifdef __cplusplus
}
#endif
