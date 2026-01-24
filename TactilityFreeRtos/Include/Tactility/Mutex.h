// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Lock.h"
#include "freertoscompat/PortCompat.h"
#include "freertoscompat/Semaphore.h"

#include <memory>
#include <cassert>

namespace tt {

/**
 * Wrapper for FreeRTOS xSemaphoreCreateMutex
 * Cannot be used from ISR context
 */
class Mutex final : public Lock {

    std::unique_ptr<std::remove_pointer_t<QueueHandle_t>, SemaphoreHandleDeleter> handle = std::unique_ptr<std::remove_pointer_t<QueueHandle_t>, SemaphoreHandleDeleter>(xSemaphoreCreateMutex());

public:

    using Lock::lock;

    explicit Mutex() {
        assert(handle != nullptr);
    }

    ~Mutex() override = default;

    /** Attempt to lock the mutex. Blocks until timeout passes or lock is acquired.
     * @param[in] timeout
     * @return success result
     */
    bool lock(TickType_t timeout) const override {
        assert(xPortInIsrContext() == pdFALSE);
        return xSemaphoreTake(handle.get(), timeout) == pdPASS;
    }

    /** Unlock the mutex */
    void unlock() const override {
        assert(xPortInIsrContext() == pdFALSE);
        xSemaphoreGive(handle.get());
    }

    /** @return the task handle of the owning task */
    TaskHandle_t getOwner() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xSemaphoreGetMutexHolder(handle.get());
    }
};

} // namespace tt
