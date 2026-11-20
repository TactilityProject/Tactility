// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <drivers/bq27427.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Standard battery pack: one 6000 mAh cell, Design Energy Scale 1 */
extern const struct Bq27427Profile TRMNL_X_GAUGE_PROFILE_ONE_CELL;

/** Battery Upgrade (X): two cells in parallel for 12000 mAh,
 *  Design Energy Scale 10 (capacities are stored in units of 10 mAh) 
 */
extern const struct Bq27427Profile TRMNL_X_GAUGE_PROFILE_TWO_CELL;

#ifdef __cplusplus
}
#endif
