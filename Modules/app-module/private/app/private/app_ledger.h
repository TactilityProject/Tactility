// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <app/manifest.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/semphr.h>
#include <tactility/freertos/task.h>

#include <stdint.h>
#include <string>
#include <unordered_map>

/**
 * A dedicated (not the task's shared default FreeRTOS notification, which app_event.cpp's
 * AppEventSubscription also uses - an unrelated event delivered to the same task could
 * otherwise unblock a waiter early) completion signal for one app instance's task, given as the
 * literal last action app_task_main() takes before vTaskDelete(). Heap-allocated with its own
 * refcount (protected by app_ledger().mutex, not atomic) rather than owned by the ledger
 * entry, since app_task_main() always erases that entry - and may run its exit path entirely -
 * before app_scheduler_stop() ever looks for it: whichever side (the exiting task, or a
 * concurrent app_scheduler_stop() that found the entry in time and is waiting on `semaphore`)
 * finishes with it last is the one that deletes `semaphore` and frees this struct.
 */
struct AppCompletionSignal {
    SemaphoreHandle_t semaphore;
    /** Starts at 1, owned by app_task_main() until its own exit. app_scheduler_stop() takes an
     * additional reference for as long as it's waiting on `semaphore`, if it finds the instance
     * still running. Reaching 0 means deletion. */
    int refcount = 1;
};

/** A registered/running app instance, as tracked internally by app-module. */
struct AppInstanceRecord {
    uint32_t id;
    const AppManifest* manifest;
    AppInstanceState state;
    /** The FreeRTOS task currently executing AppLoaderApi::run() for this instance; NULL when
     * not running. */
    TaskHandle_t task;

    /** 0 for a top-level launch (app_manager_start()). Non-zero for a modal child launched via
     * app_manager_start_for_result() - the instance that receives this child's APP_EVENT_RESULT. */
    uint32_t parent_id = 0;

    /** This instance's completion signal - see AppCompletionSignal. Set once by
     * app_scheduler_start(), never reassigned. */
    AppCompletionSignal* completion = nullptr;
};

struct AppLedger {
    std::unordered_map<std::string, const AppManifest*> manifests;
    std::unordered_map<uint32_t, AppInstanceRecord> instances;
    uint32_t next_instance_id = 1;
    Mutex mutex {};

    AppLedger() { mutex_construct(&mutex); }
    ~AppLedger() { mutex_destruct(&mutex); }
};

inline AppLedger& app_ledger() {
    static AppLedger ledger;
    return ledger;
}

/** Frees a deep-copied argv previously built by app_manager_start_with_parameters()/
 * app_manager_start_for_result() (see app_scheduler.cpp's TaskContext::argv) - each
 * individually heap-allocated string, then the array itself. Safe to call with count == 0 /
 * values == nullptr (no-op). */
inline void app_ledger_free_arguments(int count, char** values) {
    if (values == nullptr) {
        return;
    }
    for (int i = 0; i < count; i++) {
        delete[] values[i];
    }
    delete[] values;
}
