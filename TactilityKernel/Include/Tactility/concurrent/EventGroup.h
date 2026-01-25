// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include <Tactility/FreeRTOS/event_groups.h>

struct EventGroup {
    EventGroupHandle_t handle;
};

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
 * @param[in] eventGroup the event group
 * @param[in] inFlags the flags to set
 * @param[out] outFlags optional resulting flags: this is set when the return value is true
 * @return 0 on success
 */
int event_group_set(EventGroupHandle_t eventGroup, uint32_t inFlags, uint32_t* outFlags);

/**
 * Clear flags
 * @param[in] eventGroup the event group
 * @param[in] inFlags the flags to clear
 * @param[out] outFlags optional resulting flags: this is set when the return value is true
 * @return 0 on success
 */
int event_group_clear(EventGroupHandle_t eventGroup, uint32_t inFlags, uint32_t* outFlags);

/**
 * @param[in] eventGroup the event group
 * @return the current flags
 */
uint32_t event_group_get(EventGroupHandle_t eventGroup);

/**
 * Wait for flags to be set
 * @param[in] eventGroup the event group
 * @param[in] flags the flags to await
 * @param[in] awaitAll If true, await for all bits to be set. Otherwise, await for any.
 * @param[in] clearOnExit If true, clears all the bits on exit, otherwise don't clear.
 * @param[in] timeout the maximum amount of ticks to wait for flags to be set
 * @param[out] outFlags optional resulting flags: this is set when the return value is true
 * @return 0 on success
 */
int event_group_wait(
    EventGroupHandle_t eventGroup,
    uint32_t flags,
    bool awaitAll,
    bool clearOnExit,
    TickType_t timeout,
    uint32_t* outFlags
);

#ifdef __cplusplus
}
#endif
