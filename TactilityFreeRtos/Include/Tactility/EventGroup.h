// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "freertoscompat/EventGroups.h"
#include "freertoscompat/PortCompat.h"
#include "kernel/Kernel.h"

#include <cassert>
#include <memory>

namespace tt {

/** Wrapper for FreeRTOS xEventGroup and related code */
class EventGroup final {

    struct EventGroupHandleDeleter {
        static void operator()(EventGroupHandle_t handleToDelete) {
            vEventGroupDelete(handleToDelete);
        }
    };

    std::unique_ptr<std::remove_pointer_t<EventGroupHandle_t>, EventGroupHandleDeleter> handle = std::unique_ptr<std::remove_pointer_t<EventGroupHandle_t>, EventGroupHandleDeleter>(xEventGroupCreate());

public:

    EventGroup() {
        assert(xPortInIsrContext() == pdFALSE);
        assert(handle != nullptr);
    }

    ~EventGroup() {
        assert(xPortInIsrContext() == pdFALSE);
    }

    enum class Error {
        Timeout,
        Resource
    };

    /**
     * Set the flags.
     * @param[in] flags the flags to set
     * @param[out] outFlags optional resulting flags: this is set when the return value is true
     * @param[out] outError optional error output: this is set when the return value is false
     * @return true on success
     */
    bool set(uint32_t flags, uint32_t* outFlags = nullptr, Error* outError = nullptr) const {
        assert(handle);
        if (xPortInIsrContext() == pdTRUE) {
            uint32_t result;
            BaseType_t yield = pdFALSE;
            if (xEventGroupSetBitsFromISR(handle.get(), flags, &yield) == pdFAIL) {
                if (outError != nullptr) {
                    *outError = Error::Resource;
                }
                return false;
            } else {
                if (outFlags != nullptr) {
                    *outFlags = flags;
                }
                portYIELD_FROM_ISR(yield);
                return true;
            }
        } else {
            auto result = xEventGroupSetBits(handle.get(), flags);
            if (outFlags != nullptr) {
                *outFlags = result;
            }
            return true;
        }
    }

    /**
     * Clear flags
     * @param[in] flags the flags to clear
     * @param[out] outFlags optional resulting flags: this is set when the return value is true
     * @param[out] outError optional error output: this is set when the return value is false
     * @return true on success
     */
    bool clear(uint32_t flags, uint32_t* outFlags = nullptr, Error* outError = nullptr) const {
        if (xPortInIsrContext() == pdTRUE) {
            uint32_t result = xEventGroupGetBitsFromISR(handle.get());
            if (xEventGroupClearBitsFromISR(handle.get(), flags) == pdFAIL) {
                if (outError != nullptr) {
                    *outError = Error::Resource;
                }
                return false;
            }
            if (outFlags != nullptr) {
                *outFlags = result;
            }
            portYIELD_FROM_ISR(pdTRUE);
            return true;
        } else {
            auto result = xEventGroupClearBits(handle.get(), flags);
            if (outFlags != nullptr) {
                *outFlags = result;
            }
            return true;
        }
    }

    /**
     * @return the current flags
     */
    uint32_t get() const {
        if (xPortInIsrContext() == pdTRUE) {
            return xEventGroupGetBitsFromISR(handle.get());
        } else {
            return xEventGroupGetBits(handle.get());
        }
    }

    /**
     * Wait for flags to be set
     * @param[in] flags the flags to await
     * @param[in] awaitAll If true, await for all bits to be set. Otherwise, await for any.
     * @param[in] clearOnExit If true, clears all the bits on exit, otherwise don't clear.
     * @param[in] timeout the maximum amount of ticks to wait for flags to be set
     * @param[out] outFlags optional resulting flags: this is set when the return value is true
     * @param[out] outError optional error output: this is set when the return value is false
     */
    bool wait(
        uint32_t flags,
        bool awaitAll = false,
        bool clearOnExit = true,
        TickType_t timeout = kernel::MAX_TICKS,
        uint32_t* outFlags = nullptr,
        Error* outError = nullptr
    ) const {
        assert(xPortInIsrContext() == pdFALSE);

        uint32_t result_flags = xEventGroupWaitBits(
            handle.get(),
            flags,
            clearOnExit ? pdTRUE : pdFALSE,
            awaitAll ? pdTRUE : pdFALSE,
            timeout
        );

        auto invalid_flags = awaitAll
            ? ((flags & result_flags) != flags) // await all
            : ((flags & result_flags) == 0U); // await any
        if (invalid_flags) {
            if (outError != nullptr) {
                if (timeout > 0U) { // assume time-out
                    *outError = Error::Timeout;
                } else {
                    *outError = Error::Resource;
                }
            }
            return false;
        }

        if (outFlags != nullptr) {
            *outFlags = result_flags;
        }

        return true;
    }
};

} // namespace
