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

/** @warning This is internal data. Do not read/write to it directly. */
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
 *
 * @warning This is internal data. Do not read/write to it directly.
 */
struct AppStream {
    struct AppStreamBuffer buffer;
    AppInstanceId producer_id;
    TaskHandle_t producer_task;
    /** fd this stream is installed at in producer_id's fd table; set by
     * app_stream_subscribe()/app_start_with_streams(), used by
     * app_stream_unsubscribe() to find it again. */
    int producer_fd;

    struct Mutex mutex;
    /** Caller-owned group readiness is signalled on; see app_stream_subscribe(). */
    struct TaskEventGroup* event_group;
    uint32_t readable_bit;
    uint32_t writable_bit;
    bool closed;
    /** Count of AppFileOps calls currently executing against this stream; app_stream_unsubscribe()
     * waits for this to reach 0 before destructing `mutex`, since a call already blocked in
     * app_stream_await() when unsubscribe starts only wakes (it doesn't vanish) when closed. */
    int active_operations;
};

/**
 * Registers @a stream as the file-like object bound to @a producer_id's fd table at
 * @a producer_fd, claiming two bits from @a event_group for its readable/writable readiness.
 * There is at most one subscriber for a given app fd. Subscribing over an existing one
 * atomically closes it first (any task blocked on it wakes; already-buffered bytes remain
 * readable until drained) and installs @a stream in its place.
 * @param[in,out] stream caller-owned. Storage must remain valid until app_stream_unsubscribe()
 * returns. That is the only operation guaranteeing both that the fd-table binding is gone and
 * that no AppFileOps call is still executing against @a stream; app_stream_close() alone does
 * neither.
 * @param[in] buffer ring buffer storage; caller-owned, same validity requirement as @a stream.
 * @param[in] buffer_capacity size of @a buffer in bytes.
 * @param[in] event_group caller-owned; must outlive @a stream (i.e. be destructed only after
 * app_stream_unsubscribe()). Lets a task wait on this stream's readiness together with other
 * event sources sharing the same group via task_event_group_wait()/task_event_group_wait_any().
 * @retval ERROR_NOT_FOUND no instance with this id is running
 * @retval ERROR_OUT_OF_RANGE @a producer_fd is out of range
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim
 * @retval ERROR_NONE on success
 */
error_t app_stream_subscribe(struct AppStream* stream, void* buffer, size_t buffer_capacity, struct TaskEventGroup* event_group, AppInstanceId producer_id, int producer_fd);

/**
 * Binds an already-subscribed @a stream at a second fd (@a alias_fd) too, so writes to either fd
 * land in the same buffer in true write-time order (e.g. aliasing stderr onto a stdout stream, the
 * way a real tty is one device for both unless redirected). Safe to leave bound until the app
 * instance exits; teardown closes it like any other fd, and AppStream's close is idempotent.
 * @retval ERROR_NOT_FOUND no instance with @a stream's producer_id is running
 * @retval ERROR_OUT_OF_RANGE @a alias_fd is out of range
 */
error_t app_stream_bind_alias_fd(struct AppStream* stream, int alias_fd);

/**
 * Removes @a stream's binding from whichever fd table it was installed in (further use of that
 * fd then fails), wakes and waits for every AppFileOps call currently in flight against
 * @a stream to finish, then releases its two bits back to the event_group given to
 * app_stream_subscribe() and destructs its internal mutex. Only after this returns is
 * @a stream's storage safe to free or reuse.
 */
error_t app_stream_unsubscribe(struct AppStream* stream);

/**
 * Blocks until @a stream becomes readable/writable (per @a wait) or @a timeout elapses. Checks
 * the current state directly before blocking, so a permanently-true condition (e.g. closed)
 * keeps returning immediately on every call, even though the underlying event bit itself is
 * one-shot (task_event_group_wait() clears a matched bit on exit).
 * @retval ERROR_NONE the condition is true
 * @retval ERROR_TIMEOUT @a timeout elapsed
 * @retval ERROR_ISR_STATUS called from an ISR
 */
error_t app_stream_await(struct AppStream* stream, AppFileWait wait, TickType_t timeout);

/** Non-blocking: copies up to @a buffer_size currently-available bytes out of @a stream. */
size_t app_stream_read(struct AppStream* stream, void* buffer, size_t buffer_size);

/** Non-blocking: copies up to @a buffer_size bytes into @a stream, as much as currently fits.
 * Copies nothing and returns 0 once @a stream is closed. */
size_t app_stream_write(struct AppStream* stream, const void* buffer, size_t buffer_size);

/**
 * Marks @a stream closed: wakes any task blocked in app_stream_await(), and causes the other side
 * to observe EOF (reader, once drained) or a write error (writer). Safe to call while another
 * task may be blocked on @a stream, unlike app_stream_unsubscribe() (see its own doc); does not
 * by itself make @a stream's storage safe to free.
 */
error_t app_stream_close(struct AppStream* stream);

#ifdef __cplusplus
}
#endif
