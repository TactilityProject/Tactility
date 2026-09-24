// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/file.h>
#include <app/instance.h>

#include <stddef.h>
#include <stdint.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @warning Internal data. */
struct AppStreamBuffer {
    uint8_t* data;
    size_t capacity;
    size_t read_pos;
    size_t write_pos;
    size_t count;
};

/**
 * Buffered byte-oriented communication between one producer task and one consumer task. The
 * consumer owns the AppStream object and its lifetime. See app_stream_subscribe().
 * @warning Internal data.
 */
struct AppStream {
    struct AppStreamBuffer buffer;
    AppInstanceId producer_id;
    TaskHandle_t producer_task;
    int producer_fd;

    struct Mutex mutex;
    struct TaskEventGroup* event_group;
    uint32_t readable_bit;
    uint32_t writable_bit;
    bool closed;
    int active_operations;

    /** Reported by APP_IOCTL_GET_WINDOW_SIZE; 0 until app_stream_set_window_size() is called. */
    uint16_t columns;
    uint16_t rows;
};

/**
 * Registers @a stream as the file-like object bound to @a producer_id's fd table at
 * @a producer_fd, claiming two bits from @a event_group for readiness. At most one subscriber
 * per fd; subscribing over an existing one closes it first.
 * @param[in,out] stream caller-owned; must stay valid until app_stream_unsubscribe() returns.
 * @param[in] buffer ring buffer storage; caller-owned, same validity as @a stream.
 * @param[in] event_group caller-owned; must outlive @a stream.
 * @retval ERROR_NOT_FOUND no instance with this id is running
 * @retval ERROR_OUT_OF_RANGE @a producer_fd is out of range
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
error_t app_stream_subscribe(struct AppStream* stream, void* buffer, size_t buffer_capacity, struct TaskEventGroup* event_group, AppInstanceId producer_id, int producer_fd);

/**
 * Binds an already-subscribed @a stream at a second fd too, so writes to either fd land in the
 * same buffer in true write-time order (e.g. aliasing stderr onto a stdout stream).
 * @retval ERROR_NOT_FOUND no instance with @a stream's producer_id is running
 * @retval ERROR_OUT_OF_RANGE @a alias_fd is out of range
 */
error_t app_stream_bind_alias_fd(struct AppStream* stream, int alias_fd);

/**
 * Removes @a stream's fd-table binding, waits for in-flight AppFileOps calls to finish, then
 * releases its event bits and destructs its mutex. Only after this returns is @a stream safe to
 * free or reuse.
 */
error_t app_stream_unsubscribe(struct AppStream* stream);

/**
 * Blocks until @a stream becomes readable/writable (per @a wait) or @a timeout elapses.
 * @retval ERROR_NONE the condition is true
 * @retval ERROR_TIMEOUT @a timeout elapsed
 * @retval ERROR_ISR_STATUS called from an ISR
 */
error_t app_stream_await(struct AppStream* stream, AppFileWait wait, TickType_t timeout);

/** Non-blocking: copies up to @a buffer_size currently-available bytes out of @a stream. */
size_t app_stream_read(struct AppStream* stream, void* buffer, size_t buffer_size);

/** Non-blocking: copies up to @a buffer_size bytes into @a stream. Returns 0 once closed. */
size_t app_stream_write(struct AppStream* stream, const void* buffer, size_t buffer_size);

/** Marks @a stream closed: readers see EOF, writers fail. Does not free @a stream's storage. */
error_t app_stream_close(struct AppStream* stream);

/**
 * Sets the dimensions a consumer reading @a stream's bound fd(s) sees via
 * app_io_ioctl(fd, APP_IOCTL_GET_WINDOW_SIZE, ...). Callable any time after
 * app_stream_subscribe(), including before the consumer app has started reading.
 */
void app_stream_set_window_size(struct AppStream* stream, uint16_t columns, uint16_t rows);

#ifdef __cplusplus
}
#endif
