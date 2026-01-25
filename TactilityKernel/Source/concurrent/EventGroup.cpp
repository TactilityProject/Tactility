// SPDX-License-Identifier: Apache-2.0

#include <Tactility/concurrent/EventGroup.h>
#include <Tactility/Error.h>

#ifdef __cplusplus
extern "C" {
#endif

error_t event_group_set(EventGroupHandle_t eventGroup, uint32_t inFlags, uint32_t* outFlags) {
    if (xPortInIsrContext() == pdTRUE) {
        BaseType_t yield = pdFALSE;
        if (xEventGroupSetBitsFromISR(eventGroup, inFlags, &yield) == pdFAIL) {
            return ERROR_RESOURCE;
        } else {
            if (outFlags != nullptr) {
                *outFlags = inFlags;
            }
            portYIELD_FROM_ISR(yield);
            return ERROR_NONE;
        }
    } else {
        auto result = xEventGroupSetBits(eventGroup, inFlags);
        if (outFlags != nullptr) {
            *outFlags = result;
        }
        return ERROR_NONE;
    }
}

error_t event_group_clear(EventGroupHandle_t eventGroup, uint32_t inFlags, uint32_t* outFlags) {
    if (xPortInIsrContext() == pdTRUE) {
        uint32_t result = xEventGroupGetBitsFromISR(eventGroup);
        if (xEventGroupClearBitsFromISR(eventGroup, inFlags) == pdFAIL) {
            return ERROR_RESOURCE;
        }
        if (outFlags != nullptr) {
            *outFlags = result;
        }
        portYIELD_FROM_ISR(pdTRUE);
        return ERROR_NONE;
    } else {
        auto result = xEventGroupClearBits(eventGroup, inFlags);
        if (outFlags != nullptr) {
            *outFlags = result;
        }
        return ERROR_NONE;
    }
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
    uint32_t flags,
    bool awaitAll,
    bool clearOnExit,
    TickType_t timeout,
    uint32_t* outFlags
) {
    if (xPortInIsrContext()) {
        return ERROR_ISR_STATUS;
    }

    uint32_t result_flags = xEventGroupWaitBits(
        eventGroup,
        flags,
        clearOnExit ? pdTRUE : pdFALSE,
        awaitAll ? pdTRUE : pdFALSE,
        timeout
    );

    auto invalid_flags = awaitAll
        ? ((flags & result_flags) != flags) // await all
        : ((flags & result_flags) == 0U); // await any
    if (invalid_flags) {
        if (timeout > 0U) { // assume time-out
            return ERROR_TIMEOUT;
        } else {
            return ERROR_RESOURCE;
        }
    }

    if (outFlags != nullptr) {
        *outFlags = result_flags;
    }

    return ERROR_NONE;
}

#ifdef __cplusplus
}
#endif
