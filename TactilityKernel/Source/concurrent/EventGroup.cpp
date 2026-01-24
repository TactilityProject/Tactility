// SPDX-License-Identifier: Apache-2.0

#include <Tactility/concurrent/EventGroup.h>
#include <Tactility/Error.h>

#ifdef __cplusplus
extern "C" {
#endif

int event_group_set(EventGroupHandle_t eventGroup, uint32_t flags, uint32_t* outFlags) {
    assert(eventGroup);
    if (xPortInIsrContext() == pdTRUE) {
        BaseType_t yield = pdFALSE;
        if (xEventGroupSetBitsFromISR(eventGroup, flags, &yield) == pdFAIL) {
            return ERROR_RESOURCE;
        } else {
            if (outFlags != nullptr) {
                *outFlags = flags;
            }
            portYIELD_FROM_ISR(yield);
            return 0;
        }
    } else {
        auto result = xEventGroupSetBits(eventGroup, flags);
        if (outFlags != nullptr) {
            *outFlags = result;
        }
        return 0;
    }
}

int event_group_clear(EventGroupHandle_t eventGroup, uint32_t flags, uint32_t* outFlags) {
    assert(eventGroup);
    if (xPortInIsrContext() == pdTRUE) {
        uint32_t result = xEventGroupGetBitsFromISR(eventGroup);
        if (xEventGroupClearBitsFromISR(eventGroup, flags) == pdFAIL) {
            return ERROR_RESOURCE;
        }
        if (outFlags != nullptr) {
            *outFlags = result;
        }
        portYIELD_FROM_ISR(pdTRUE);
        return 0;
    } else {
        auto result = xEventGroupClearBits(eventGroup, flags);
        if (outFlags != nullptr) {
            *outFlags = result;
        }
        return 0;
    }
}

uint32_t event_group_get(EventGroupHandle_t eventGroup) {
    assert(eventGroup);
    if (xPortInIsrContext() == pdTRUE) {
        return xEventGroupGetBitsFromISR(eventGroup);
    } else {
        return xEventGroupGetBits(eventGroup);
    }
}

int event_group_wait(
    EventGroupHandle_t eventGroup,
    uint32_t flags,
    bool awaitAll,
    bool clearOnExit,
    TickType_t timeout,
    uint32_t* outFlags
) {
    assert(eventGroup != nullptr);
    assert(xPortInIsrContext() == pdFALSE);

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

    return 0;
}

#ifdef __cplusplus
}
#endif
