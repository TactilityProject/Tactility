// SPDX-License-Identifier: Apache-2.0
/**
 * MessageQueue is a wrapper for FreeRTOS xQueue functionality.
 * There is no additional thread-safety on top of the xQueue functionality,
 * so make sure you create a lock if needed.
 */
#pragma once

#include <memory>
#include <cassert>

#include "freertoscompat/PortCompat.h"
#include "freertoscompat/Queue.h"

namespace tt {

/**
 * Wraps xQueue functionality.
 * Calls can be done from ISR context unless otherwise specified.
 */
class MessageQueue final {

    static QueueHandle_t createQueue(uint32_t capacity, uint32_t messageSize) {
        assert(capacity > 0U);
        assert(messageSize > 0U);
        return xQueueCreate(capacity, messageSize);
    }

    struct QueueHandleDeleter {
        static void operator()(QueueHandle_t handleToDelete) {
            vQueueDelete(handleToDelete);
        }
    };

    std::unique_ptr<std::remove_pointer_t<QueueHandle_t>, QueueHandleDeleter> handle;

public:
    /**
     * Allocate message queue
     * @param[in] capacity Maximum messages in queue
     * @param[in] messageSize The size in bytes of a single message
     */
    MessageQueue(uint32_t capacity, uint32_t messageSize) : handle(createQueue(capacity, messageSize)) {
        assert(handle != nullptr);
        assert(xPortInIsrContext() == pdFALSE);
    }

    /**
     * @warning Don't call this from ISR context
     */
    ~MessageQueue() {
        assert(xPortInIsrContext() == pdFALSE);
    }

    /**
     * Post a message to the queue.
     * The message is queued by copy, not by reference.
     * @param[in] message A pointer to a message. The message will be copied into a buffer.
     * @param[in] timeout
     * @return success result
     */
    bool put(const void* message, TickType_t timeout) const {
        assert(handle != nullptr);
        assert(message != nullptr);

        if (xPortInIsrContext() == pdTRUE) {
            if (timeout != 0U) {
                return false;
            }

            BaseType_t yield = pdFALSE;
            if (xQueueSendToBackFromISR(handle.get(), message, &yield) != pdTRUE) {
                return false;
            }

            portYIELD_FROM_ISR(yield);
            return true;
        } else {
            return xQueueSendToBack(handle.get(), message, timeout) == pdPASS;
        }
    }

    /**
     * Get message from queue
     * @param[out] message A pointer to an already allocated message object
     * @param[in] timeout
     * @return success result
     */
    bool get(void* message, TickType_t timeout) const {
        assert(handle != nullptr);
        assert(message != nullptr);

        if (xPortInIsrContext() == pdTRUE) {
            if (timeout != 0U) {
                return false;
            }

            BaseType_t yield = pdFALSE;
            if (xQueueReceiveFromISR(handle.get(), message, &yield) != pdPASS) {
                return false;
            }

            portYIELD_FROM_ISR(yield);
            return true;
        } else {
            return xQueueReceive(handle.get(), message, timeout) == pdPASS;
        }
    }

    /**
     * @return The amount of messages in the queue.
     */
    uint32_t getCount() const {
        assert(handle != nullptr);
        return (xPortInIsrContext() == pdTRUE)
            ? uxQueueMessagesWaitingFromISR(handle.get())
            : uxQueueMessagesWaiting(handle.get());
    }

    /**
     * Reset queue
     * @warning Don't call this from ISR context
     * @return success result
     */
    void reset() const {
        assert(handle != nullptr);
        assert(xPortInIsrContext() == pdFALSE);
        xQueueReset(handle.get());
    }
};

} // namespace
