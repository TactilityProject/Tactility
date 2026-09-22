// SPDX-License-Identifier: Apache-2.0
#include <app/stream.h>

#include <app/private/fd_table.h>
#include <app/private/ledger.h>
#include <app/private/stream_internal.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/error.h>

namespace {

// Caller must hold stream->mutex.
bool is_readable_locked(AppStream* stream) {
    return stream->buffer.count > 0 || stream->closed;
}

// Caller must hold stream->mutex.
bool is_writable_locked(AppStream* stream) {
    return stream->buffer.count < stream->buffer.capacity || stream->closed;
}

// Brackets one AppFileOps call against `stream` for app_stream_unsubscribe()'s drain wait: a
// call already blocked in app_stream_await() when unsubscribe starts only wakes, it doesn't
// vanish, so unsubscribe must know it's still running before destructing stream->mutex.
class StreamOperationGuard {
public:
    explicit StreamOperationGuard(AppStream* stream) : stream_(stream) {
        mutex_lock(&stream_->mutex);
        stream_->active_operations++;
        mutex_unlock(&stream_->mutex);
    }

    ~StreamOperationGuard() {
        mutex_lock(&stream_->mutex);
        stream_->active_operations--;
        mutex_unlock(&stream_->mutex);
    }

    StreamOperationGuard(const StreamOperationGuard&) = delete;
    StreamOperationGuard& operator=(const StreamOperationGuard&) = delete;

private:
    AppStream* stream_;
};

ssize_t stream_file_read(void* object, void* buffer, size_t size) {
    auto* stream = static_cast<AppStream*>(object);
    StreamOperationGuard guard(stream);
    while (true) {
        if (app_stream_await(stream, APP_FILE_WAIT_READABLE, portMAX_DELAY) != ERROR_NONE) {
            return -1;
        }
        size_t read = app_stream_read(stream, buffer, size);
        if (read > 0) {
            return static_cast<ssize_t>(read);
        }
        mutex_lock(&stream->mutex);
        bool is_closed = stream->closed;
        mutex_unlock(&stream->mutex);
        if (is_closed) {
            return 0; // EOF: readable-because-closed, and nothing left buffered
        }
        // Woke readable but another party drained it first. Re-await rather than assume EOF;
        // this is a defensive fallback since AppStream expects a single reader.
    }
}

ssize_t stream_file_write(void* object, const void* buffer, size_t size) {
    auto* stream = static_cast<AppStream*>(object);
    StreamOperationGuard guard(stream);
    if (app_stream_await(stream, APP_FILE_WAIT_WRITABLE, portMAX_DELAY) != ERROR_NONE) {
        return -1;
    }
    // app_stream_write() itself refuses to copy anything once closed (checked under the same
    // lock as the copy, so a close() racing right after this await() can't slip bytes in), and
    // reports that as 0, indistinguishable here from "woke writable, nothing to copy" without
    // checking closed separately.
    size_t written = app_stream_write(stream, buffer, size);
    if (written > 0) {
        return static_cast<ssize_t>(written);
    }
    mutex_lock(&stream->mutex);
    bool is_closed = stream->closed;
    mutex_unlock(&stream->mutex);
    return is_closed ? -1 : 0; // broken pipe, or a spurious wake with nothing to copy
}

error_t stream_file_close(void* object) {
    auto* stream = static_cast<AppStream*>(object);
    StreamOperationGuard guard(stream);
    return app_stream_close(stream);
}

error_t stream_file_await(void* object, AppFileWait wait, TickType_t timeout) {
    auto* stream = static_cast<AppStream*>(object);
    StreamOperationGuard guard(stream);
    return app_stream_await(stream, wait, timeout);
}

uint32_t stream_file_poll(void* object) {
    auto* stream = static_cast<AppStream*>(object);
    StreamOperationGuard guard(stream);
    mutex_lock(&stream->mutex);
    uint32_t bits = 0;
    if (is_readable_locked(stream)) {
        bits |= APP_FILE_READABLE;
    }
    if (is_writable_locked(stream)) {
        bits |= APP_FILE_WRITABLE;
    }
    mutex_unlock(&stream->mutex);
    return bits;
}

// Fused with app_fd_table_get_and_retain()'s own lock, so a caller that gets a live AppFile back
// is already counted here before it can be paused and raced by app_stream_unsubscribe()'s drain
// wait (see StreamOperationGuard above for the equivalent per-call bracketing).
void stream_file_retain(void* object) {
    auto* stream = static_cast<AppStream*>(object);
    mutex_lock(&stream->mutex);
    stream->active_operations++;
    mutex_unlock(&stream->mutex);
}

void stream_file_release(void* object) {
    auto* stream = static_cast<AppStream*>(object);
    mutex_lock(&stream->mutex);
    stream->active_operations--;
    mutex_unlock(&stream->mutex);
}

constexpr AppFileOps STREAM_OPS = {
    .read = stream_file_read,
    .write = stream_file_write,
    .close = stream_file_close,
    .await = stream_file_await,
    .poll = stream_file_poll,
    .retain = stream_file_retain,
    .release = stream_file_release,
};

} // namespace

extern "C" {

const AppFileOps* app_stream_ops(void) {
    return &STREAM_OPS;
}

error_t app_stream_subscribe(AppStream* stream, void* buffer, size_t buffer_capacity, TaskEventGroup* event_group, AppInstanceId producer_id, int producer_fd) {
    if (producer_fd < 0 || producer_fd >= APP_MAX_FDS) {
        return ERROR_OUT_OF_RANGE;
    }

    uint32_t readable_bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &readable_bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }
    uint32_t writable_bit;
    claim_result = task_event_group_claim_bit(event_group, &writable_bit);
    if (claim_result != ERROR_NONE) {
        task_event_group_release_bit(event_group, readable_bit);
        return claim_result;
    }

    stream->producer_id = producer_id;
    stream->producer_fd = producer_fd;
    stream->event_group = event_group;
    stream->readable_bit = readable_bit;
    stream->writable_bit = writable_bit;
    stream->buffer.data = static_cast<uint8_t*>(buffer);
    stream->buffer.capacity = buffer_capacity;
    stream->buffer.read_pos = 0;
    stream->buffer.write_pos = 0;
    stream->buffer.count = 0;
    stream->closed = false;
    stream->active_operations = 0;
    mutex_construct(&stream->mutex);

    // Held across the fd-table lookup and bind so this can't race app_fd_table_teardown():
    // both teardown call sites (scheduler.cpp, manager.cpp) hold this same ledger mutex for
    // their whole teardown call, and app_fd_table_bind()/close()/get() check for it (see
    // fd_table.h's AppFdTable::shutting_down).
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(producer_id);
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        mutex_destruct(&stream->mutex);
        task_event_group_release_bit(event_group, readable_bit);
        task_event_group_release_bit(event_group, writable_bit);
        return ERROR_NOT_FOUND;
    }
    stream->producer_task = iterator->second.task;
    error_t bind_result = app_fd_table_bind(&iterator->second.fd_table, producer_fd, &STREAM_OPS, stream, /*suppress_console_tee=*/false);
    mutex_unlock(&ledger.mutex);

    if (bind_result != ERROR_NONE) {
        mutex_destruct(&stream->mutex);
        task_event_group_release_bit(event_group, readable_bit);
        task_event_group_release_bit(event_group, writable_bit);
    }
    return bind_result;
}

error_t app_stream_bind_alias_fd(AppStream* stream, int alias_fd) {
    if (alias_fd < 0 || alias_fd >= APP_MAX_FDS) {
        return ERROR_OUT_OF_RANGE;
    }

    // Same locking rationale as app_stream_subscribe(): held across the fd-table lookup and bind
    // so this can't race app_fd_table_teardown().
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(stream->producer_id);
    if (iterator == ledger.instances.end()) {
        mutex_unlock(&ledger.mutex);
        return ERROR_NOT_FOUND;
    }
    error_t result = app_fd_table_bind(&iterator->second.fd_table, alias_fd, &STREAM_OPS, stream, /*suppress_console_tee=*/false);
    mutex_unlock(&ledger.mutex);
    return result;
}

error_t app_stream_unsubscribe(AppStream* stream) {
    // Held across the fd-table lookup, get, and close. See app_stream_subscribe()'s own
    // comment on why this must stay exclusive with app_fd_table_teardown().
    auto& ledger = app_ledger();
    mutex_lock(&ledger.mutex);
    auto iterator = ledger.instances.find(stream->producer_id);
    if (iterator != ledger.instances.end()) {
        AppFdTable* table = &iterator->second.fd_table;
        AppFile current {};
        if (app_fd_table_get(table, stream->producer_fd, &current) && current.object == stream) {
            app_fd_table_close(table, stream->producer_fd); // -> stream_file_close() -> app_stream_close()
        }
    }
    mutex_unlock(&ledger.mutex);

    app_stream_close(stream); // idempotent; wakes anyone already blocked in app_stream_await()

    // Waking a blocked call doesn't mean it has finished. It still has to get scheduled, notice
    // it's closed, and return. Wait for that before destructing mutex/bits below, mirroring
    // scheduler.cpp's reap_self()/reaper_task_main() waiting out a task before freeing its stack.
    while (true) {
        mutex_lock(&stream->mutex);
        int active_operations = stream->active_operations;
        mutex_unlock(&stream->mutex);
        if (active_operations == 0) {
            break;
        }
        taskYIELD();
    }

    task_event_group_release_bit(stream->event_group, stream->readable_bit);
    task_event_group_release_bit(stream->event_group, stream->writable_bit);
    mutex_destruct(&stream->mutex);
    return ERROR_NONE;
}

error_t app_stream_await(AppStream* stream, AppFileWait wait, TickType_t timeout) {
    mutex_lock(&stream->mutex);
    bool ready = (wait == APP_FILE_WAIT_READABLE) ? is_readable_locked(stream) : is_writable_locked(stream);
    mutex_unlock(&stream->mutex);
    if (ready) {
        return ERROR_NONE;
    }

    uint32_t bit = (wait == APP_FILE_WAIT_READABLE) ? stream->readable_bit : stream->writable_bit;
    return task_event_group_wait(stream->event_group, bit, /*await_all=*/false, nullptr, timeout);
}

size_t app_stream_read(AppStream* stream, void* buffer, size_t buffer_size) {
    mutex_lock(&stream->mutex);
    AppStreamBuffer& ring = stream->buffer;
    size_t to_copy = buffer_size < ring.count ? buffer_size : ring.count;
    for (size_t i = 0; i < to_copy; i++) {
        static_cast<uint8_t*>(buffer)[i] = ring.data[(ring.read_pos + i) % ring.capacity];
    }
    if (ring.capacity > 0) {
        ring.read_pos = (ring.read_pos + to_copy) % ring.capacity;
    }
    ring.count -= to_copy;
    TaskEventGroup* event_group = stream->event_group;
    uint32_t writable_bit = stream->writable_bit;
    mutex_unlock(&stream->mutex);
    if (to_copy > 0) {
        task_event_group_signal(event_group, writable_bit); // space freed up
    }
    return to_copy;
}

size_t app_stream_write(AppStream* stream, const void* buffer, size_t buffer_size) {
    mutex_lock(&stream->mutex);
    if (stream->closed) {
        mutex_unlock(&stream->mutex);
        return 0;
    }
    AppStreamBuffer& ring = stream->buffer;
    size_t available = ring.capacity - ring.count;
    size_t to_copy = buffer_size < available ? buffer_size : available;
    for (size_t i = 0; i < to_copy; i++) {
        ring.data[(ring.write_pos + i) % ring.capacity] = static_cast<const uint8_t*>(buffer)[i];
    }
    if (ring.capacity > 0) {
        ring.write_pos = (ring.write_pos + to_copy) % ring.capacity;
    }
    ring.count += to_copy;
    TaskEventGroup* event_group = stream->event_group;
    uint32_t readable_bit = stream->readable_bit;
    mutex_unlock(&stream->mutex);
    if (to_copy > 0) {
        task_event_group_signal(event_group, readable_bit); // data became available
    }
    return to_copy;
}

error_t app_stream_close(AppStream* stream) {
    mutex_lock(&stream->mutex);
    stream->closed = true;
    TaskEventGroup* event_group = stream->event_group;
    uint32_t readable_bit = stream->readable_bit;
    uint32_t writable_bit = stream->writable_bit;
    mutex_unlock(&stream->mutex);
    task_event_group_signal(event_group, readable_bit);
    task_event_group_signal(event_group, writable_bit);
    return ERROR_NONE;
}

} // extern "C"
