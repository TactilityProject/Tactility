// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <Tactility/Error.h>
#include <Tactility/FreeRTOS/event_groups.h>

static inline void event_group_construct(EventGroupHandle_t* eventGroup) {
    assert(xPortInIsrContext() == pdFALSE);
    *eventGroup = xEventGroupCreate();
}

static inline void event_group_destruct(EventGroupHandle_t* eventGroup) {
    assert(xPortInIsrContext() == pdFALSE);
    assert(*eventGroup != NULL);
    vEventGroupDelete(*eventGroup);
    *eventGroup = NULL;
}

/**
 * Set the flags.
 *
 * @param[in] eventGroup the event group
 * @param[in] flags the flags to set
 * @retval ERROR_RESOURCE when setting failed
 * @retval ERROR_NONE
 */
error_t event_group_set(EventGroupHandle_t eventGroup, uint32_t flags);

/**
 * Clear flags
 *
 * @param[in] eventGroup the event group
 * @param[in] flags the flags to clear
 * @retval ERROR_RESOURCE when clearing failed
 * @retval ERROR_NONE
 */
error_t event_group_clear(EventGroupHandle_t eventGroup, uint32_t flags);

/**
 * @param[in] eventGroup the event group
 * @return the bitset (always succeeds)
 */
uint32_t event_group_get(EventGroupHandle_t eventGroup);

/**
 * Wait for flags to be set
 *
 * @param[in] eventGroup the event group
 * @param[in] inFlags the flags to await
 * @param[in] awaitAll If true, await for all bits to be set. Otherwise, await for any.
 * @param[in] clearOnExit If true, clears all the bits on exit, otherwise don't clear.
 * @param[out] outFlags If set to non-NULL value, this will hold the resulting flags. Only set when return value is ERROR_NONE
 * @param[in] timeout the maximum amount of ticks to wait for flags to be set
 * @retval ERROR_ISR_STATUS when the function was called from an ISR context
 * @retval ERROR_TIMEOUT
 * @retval ERROR_RESOURCE when flags were triggered, but not in a way that was expected (e.g. waiting for all flags, but was only partially set)
 * @retval ERROR_NONE
 */
error_t event_group_wait(
    EventGroupHandle_t eventGroup,
    uint32_t inFlags,
    bool awaitAll,
    bool clearOnExit,
    uint32_t* outFlags,
    TickType_t timeout
);

#ifdef __cplusplus
}
#endif
