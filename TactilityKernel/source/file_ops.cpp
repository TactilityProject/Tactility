// SPDX-License-Identifier: Apache-2.0
#include <tactility/file_ops.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/time.h>

#include <atomic>
#include <new>

#define TAG "file_ops"

// Refcounted: one share for the subscriber, one for each queued item that references it.
// Lets file_ops_unsubscribe() sever the subscription at any time, also while work is still queued.
struct FileOpsLink {
    Mutex mutex;
    uint32_t owners = 1;
    bool subscribed = true;
    uint32_t completed = 0;

    FileOpsLink() { mutex_construct(&mutex); }
    ~FileOpsLink() { mutex_destruct(&mutex); }
};

namespace {

// Large enough for the file and settings helpers and for loading apps. It must be in internal RAM to access flash
constexpr size_t WORKER_STACK_SIZE = 8192;

struct Item {
    file_ops_work_fn_t work;
    void* context;
    FileOpsSubscription* subscription;
    FileOpsLink* link;
    Item* next;
};

// Small enough to stay in internal RAM, which is also where its mutex must be
struct State {
    Mutex mutex;
    Item* head = nullptr;
    Item* tail = nullptr;
    bool running = false;
    // The worker that has ended or is ending, as it can't free itself
    Thread* stale = nullptr;
    // The task of the running worker, null when there is none
    std::atomic<TaskHandle_t> workerTask { nullptr };

    State() { mutex_construct(&mutex); }
};

State& state() {
    static State instance;
    return instance;
}

const MemoryPolicy ITEM_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = 0 };

void release_link(FileOpsLink* link) {
    mutex_lock(&link->mutex);
    const bool last = (--link->owners == 0);
    mutex_unlock(&link->mutex);
    if (last) {
        delete link;
    }
}

void complete(Item* item) {
    FileOpsLink* link = item->link;
    if (link == nullptr) {
        return;
    }
    mutex_lock(&link->mutex);
    if (link->subscribed) {
        link->completed++;
        task_event_group_signal(item->subscription->internal.event_group, item->subscription->bit);
    }
    const bool last = (--link->owners == 0);
    mutex_unlock(&link->mutex);
    if (last) {
        delete link;
    }
}

Item* pop_or_stop(State& s) {
    mutex_lock(&s.mutex);
    Item* item = s.head;
    if (item != nullptr) {
        s.head = item->next;
        if (s.head == nullptr) {
            s.tail = nullptr;
        }
    } else {
        s.running = false;
    }
    mutex_unlock(&s.mutex);
    return item;
}

int32_t worker_main(void* context) {
    auto& s = *static_cast<State*>(context);
    s.workerTask.store(xTaskGetCurrentTaskHandle());
    while (Item* item = pop_or_stop(s)) {
        item->work(item->context);
        complete(item);
        memory_free(item);
    }
    s.workerTask.store(nullptr);
    return 0;
}

// Must be called with the state mutex held and no worker running
bool start_worker(State& s) {
    if (s.stale != nullptr) {
        thread_join(s.stale, portMAX_DELAY, 1);
        thread_free(s.stale);
        s.stale = nullptr;
    }

    Thread* thread = thread_alloc_full("file_ops", WORKER_STACK_SIZE, worker_main, &s, -1);
    if (thread == nullptr) {
        return false;
    }
    s.running = true;
    if (thread_start(thread) != ERROR_NONE) {
        s.running = false;
        thread_free(thread);
        return false;
    }
    s.stale = thread;
    return true;
}

}

extern "C" {

error_t file_ops_subscribe(FileOpsSubscription* sub, TaskEventGroup* event_group) {
    uint32_t bit;
    const error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    auto* link = new (std::nothrow) FileOpsLink();
    if (link == nullptr) {
        task_event_group_release_bit(event_group, bit);
        return ERROR_OUT_OF_MEMORY;
    }

    sub->bit = bit;
    sub->internal.event_group = event_group;
    sub->internal.link = link;
    return ERROR_NONE;
}

error_t file_ops_unsubscribe(FileOpsSubscription* sub) {
    FileOpsLink* link = sub->internal.link;
    if (link == nullptr) {
        return ERROR_NOT_FOUND;
    }
    // Caller-exclusive: the worker never reads this field
    sub->internal.link = nullptr;

    mutex_lock(&link->mutex);
    // The worker reads these under the same lock while subscribed is true
    TaskEventGroup* event_group = sub->internal.event_group;
    const uint32_t bit = sub->bit;
    sub->internal.event_group = nullptr;
    link->subscribed = false;
    const bool last = (--link->owners == 0);
    mutex_unlock(&link->mutex);

    // Nothing signals the bit anymore, as decided in the critical section above
    task_event_group_release_bit(event_group, bit);

    if (last) {
        delete link;
    }
    return ERROR_NONE;
}

error_t file_ops_queue(file_ops_work_fn_t work, void* context, FileOpsSubscription* sub) {
    FileOpsLink* link = nullptr;
    if (sub != nullptr) {
        link = sub->internal.link;
        if (link == nullptr) {
            return ERROR_INVALID_ARGUMENT;
        }
    }

    auto* item = static_cast<Item*>(memory_alloc_with_policy(sizeof(Item), &ITEM_POLICY));
    if (item == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    *item = { work, context, sub, link, nullptr };

    if (link != nullptr) {
        mutex_lock(&link->mutex);
        link->owners++;
        mutex_unlock(&link->mutex);
    }

    auto& s = state();
    mutex_lock(&s.mutex);
    if (s.tail != nullptr) {
        s.tail->next = item;
    } else {
        s.head = item;
    }
    s.tail = item;

    if (!s.running && !start_worker(s)) {
        // The queue was empty before this item, because no worker was running
        s.head = nullptr;
        s.tail = nullptr;
        mutex_unlock(&s.mutex);
        LOG_E(TAG, "Failed to start the file operations task");
        if (link != nullptr) {
            release_link(link);
        }
        memory_free(item);
        return ERROR_RESOURCE;
    }
    mutex_unlock(&s.mutex);
    return ERROR_NONE;
}

error_t file_ops_poll(FileOpsSubscription* sub) {
    FileOpsLink* link = sub->internal.link;
    if (link == nullptr) {
        return ERROR_TIMEOUT;
    }

    mutex_lock(&link->mutex);
    const bool available = link->completed > 0;
    if (available) {
        link->completed--;
    }
    mutex_unlock(&link->mutex);
    return available ? ERROR_NONE : ERROR_TIMEOUT;
}

bool file_ops_is_worker(void) {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    return current != nullptr && state().workerTask.load() == current;
}

error_t file_ops_await(FileOpsSubscription* sub, TickType_t timeout) {
    if (file_ops_poll(sub) == ERROR_NONE) {
        return ERROR_NONE;
    }
    const TickType_t start = get_ticks();
    while (true) {
        const TickType_t elapsed = get_ticks() - start;
        if (elapsed >= timeout) {
            return ERROR_TIMEOUT;
        }
        if (task_event_group_wait(sub->internal.event_group, sub->bit, false, nullptr, timeout - elapsed) != ERROR_NONE) {
            return ERROR_TIMEOUT;
        }
        if (file_ops_poll(sub) == ERROR_NONE) {
            return ERROR_NONE;
        }
    }
}

}
