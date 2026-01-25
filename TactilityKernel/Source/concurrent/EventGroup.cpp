// SPDX-License-Identifier: Apache-2.0

#include <Tactility/concurrent/EventGroup.h>
#include <Tactility/Error.h>

#ifdef __cplusplus
extern "C" {
#endif

error_t event_group_set(EventGroupHandle_t eventGroup, uint32_t inFlags) {
    if (xPortInIsrContext() == pdTRUE) {
        BaseType_t yield = pdFALSE;
        if (xEventGroupSetBitsFromISR(eventGroup, inFlags, &yield) == pdFAIL) {
            return ERROR_RESOURCE;
        }
        portYIELD_FROM_ISR(yield);
    } else {
        xEventGroupSetBits(eventGroup, inFlags);
    }
    return ERROR_NONE;
}

error_t event_group_clear(EventGroupHandle_t eventGroup, uint32_t flags) {
    if (xPortInIsrContext() == pdTRUE) {
        if (xEventGroupClearBitsFromISR(eventGroup, flags) == pdFAIL) {
            return ERROR_RESOURCE;
        }
        portYIELD_FROM_ISR(pdTRUE);
    } else {
        if (xEventGroupClearBits(eventGroup, flags) == pdFAIL) {
            return ERROR_RESOURCE;
        }
    }
    return ERROR_NONE;
}

uint32_t event_group_get(EventGroupHandle_t eventGroup) {
    if (xPortInIsrContext() == pdTRUE) {
        return xEventGroupGetBitsFromISR(eventGroup);
    } else {
        return xEventGroupGetBits(eventGroup);
    }
}

error_t event_group_wait(
    EventGroupHandle_t eventGroup,
    uint32_t inFlags,
    bool awaitAll,
    bool clearOnExit,
    uint32_t* outFlags,
    TickType_t timeout
) {
    if (xPortInIsrContext()) {
        return ERROR_ISR_STATUS;
    }

    uint32_t result_flags = xEventGroupWaitBits(
        eventGroup,
        inFlags,
        clearOnExit ? pdTRUE : pdFALSE,
        awaitAll ? pdTRUE : pdFALSE,
        timeout
    );

    auto invalid_flags = awaitAll
        ? ((inFlags & result_flags) != inFlags) // await all
        : ((inFlags & result_flags) == 0U); // await any

    if (invalid_flags) {
        const uint32_t matched = inFlags & result_flags;
        if (matched == 0U) {
            return ERROR_TIMEOUT;
        }
        return ERROR_RESOURCE;
    }

    if (outFlags != nullptr) {
        *outFlags = result_flags;
    }

    return ERROR_NONE;
}

#ifdef __cplusplus
}
#endif
