// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Lock.h"
#include "freertoscompat/PortCompat.h"
#include "freertoscompat/Semaphore.h"

#include <cassert>
#include <memory>

#ifdef ESP_PLATFORM
#include <esp_log.h>
#endif

namespace tt {

/**
 * Wrapper for xSemaphoreCreateBinary (max count == 1) and xSemaphoreCreateCounting (max count > 1)
 * Can be used from ISR context, but cannot be created/destroyed from such a context.
 */
class Semaphore final : public Lock {

    static QueueHandle_t createHandle(uint32_t maxCount, uint32_t initialAvailable) {
        assert(maxCount > 0U);
        assert(initialAvailable <= maxCount);

        if (maxCount == 1U) {
            auto result = xSemaphoreCreateBinary();
            if (initialAvailable != 0U) {
                auto give_result = xSemaphoreGive(result);
                assert(give_result == pdPASS);
            }
            return result;
        } else {
            return xSemaphoreCreateCounting(maxCount, initialAvailable);
        }
    }

    std::unique_ptr<std::remove_pointer_t<QueueHandle_t>, SemaphoreHandleDeleter> handle;

public:

    using Lock::lock;

    /**
     * Cannot be called from ISR context.
     * @param[in] maxAvailable The maximum count
     * @param[in] initialAvailable The initial count
     */
    Semaphore(uint32_t maxAvailable, uint32_t initialAvailable) : handle(createHandle(maxAvailable, initialAvailable)) {
        assert(xPortInIsrContext() == pdFALSE);
        assert(handle != nullptr);
    }

    /**
     * Cannot be called from IRQ/ISR mode.
     * @param[in] maxAvailable The maximum count
     */
    explicit Semaphore(uint32_t maxAvailable) : Semaphore(maxAvailable, maxAvailable) {}

    /** Cannot be called from IRQ/ISR mode. */
    ~Semaphore() override {
        assert(xPortInIsrContext() == pdFALSE);
    }

    Semaphore(Semaphore& other) : handle(std::move(other.handle)) {}

    /**
     * Acquire the semaphore
     * @param[in] timeout
     * @return true on success
     */
    bool acquire(TickType_t timeout) const {
        if (xPortInIsrContext() == pdTRUE) {
            if (timeout != 0U) {
                return false;
            }

            BaseType_t yield = pdFALSE;
            if (xSemaphoreTakeFromISR(handle.get(), &yield) != pdPASS) {
                return false;
            }

            portYIELD_FROM_ISR(yield);
            return true;
        } else {
            return xSemaphoreTake(handle.get(), timeout) == pdPASS;
        }
    }

    /**
     * Release the semaphore
     * @return true on success
     */
    bool release() const {
        if (xPortInIsrContext() == pdTRUE) {
            BaseType_t yield = pdFALSE;
            if (xSemaphoreGiveFromISR(handle.get(), &yield) != pdTRUE) {
                return false;
            }

            portYIELD_FROM_ISR(yield);
            return true;
        } else {
            return xSemaphoreGive(handle.get()) == pdPASS;
        }
    }

    /** @return the acquisition count */
    uint32_t getAvailable() const {
        if (xPortInIsrContext() == pdTRUE) {
            return uxSemaphoreGetCountFromISR(handle.get());
        } else {
            return uxSemaphoreGetCount(handle.get());
        }
    }

    // region Lock

    /** Calls acquire() */
    bool lock(TickType_t timeout) const override { return acquire(timeout); }

    /** Calls release() */
    void unlock() const override { release(); }

    // endregion
};

} // namespace
