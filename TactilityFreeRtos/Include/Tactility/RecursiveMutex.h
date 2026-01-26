// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "Lock.h"
#include "freertoscompat/PortCompat.h"
#include "freertoscompat/Semaphore.h"

#include <memory>
#include <cassert>

namespace tt {

/**
 * Wrapper for FreeRTOS xSemaphoreCreateRecursiveMutex
 * Cannot be used from ISR context
 */
class RecursiveMutex final : public Lock {

    std::unique_ptr<std::remove_pointer_t<QueueHandle_t>, SemaphoreHandleDeleter> handle = std::unique_ptr<std::remove_pointer_t<QueueHandle_t>, SemaphoreHandleDeleter>(xSemaphoreCreateRecursiveMutex());

public:

    using Lock::lock;

    explicit RecursiveMutex() {
        assert(handle != nullptr);
    }

    ~RecursiveMutex() override = default;

    /**
     * Attempt to lock the mutex. Blocks until timeout passes or lock is acquired.
     * @param[in] timeout
     * @return success result
     */
    bool lock(TickType_t timeout) const override {
        assert(xPortInIsrContext() == pdFALSE);
        return xSemaphoreTakeRecursive(handle.get(), timeout) == pdPASS;
    }

    /** Unlock the mutex */
    void unlock() const override {
        assert(xPortInIsrContext() == pdFALSE);
        xSemaphoreGiveRecursive(handle.get());
    }

    /** @return the owner of the thread */
     TaskHandle_t getOwner() const {
        assert(xPortInIsrContext() == pdFALSE);
        return xSemaphoreGetMutexHolder(handle.get());
    }
};

} // namespace
