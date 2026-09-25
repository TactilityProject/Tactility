// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <app/manifest.h>
#include <app/package_manifest.h>
#include <app/private/fd_table.h>

#include <TactilityCpp/Allocator.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/semphr.h>
#include <tactility/freertos/task.h>

#include <stdint.h>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * A dedicated completion signal(1) for one app instance's task, given as the
 * literal last action app_task_main() takes before vTaskDelete().
 * Heap-allocated with its own refcount (protected by app_ledger().mutex, not atomic)
 * rather than owned by the ledger entry, since app_task_main() always erases that entry -
 * and may run its exit path entirely - before app_scheduler_stop() ever looks for it:
 * Whichever side(2) finishes with it last is the one that deletes `semaphore` and frees this struct.
 *
 * (1) Not the task's shared default FreeRTOS notification, which app_event.cpp's
 * AppEventSubscription also uses - an unrelated event delivered to the same task could
 * otherwise unblock a waiter early.
 * (2) The exiting task, or a concurrent app_scheduler_stop() that found the entry in time and is waiting on `semaphore`
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
    /** NULL for an instance started via app_execute() (app/execute.h; no manifest involved). */
    const AppManifest* manifest;
    AppInstanceState state;
    /** The FreeRTOS task currently executing AppLoaderApi::run() for this instance; NULL when not running. */
    TaskHandle_t task;

    /** 0 for a top-level launch (app_start()). Non-zero for a modal child launched via
     * app_start_for_result() - the instance that receives this child's APP_EVENT_RESULT. */
    uint32_t parent_id = 0;

    /** This instance's completion signal - see AppCompletionSignal. Set once by
     * app_scheduler_start(), never reassigned. */
    AppCompletionSignal* completion = nullptr;

    /** This instance's fd table. Constructed by app_manager_start_internal() before insertion
     * into AppLedger::instances, torn down (every open fd closed) when the instance's task exits. */
    AppFdTable fd_table {};

    /** This instance's environment, as "NAME=VALUE" strings - seeded from AppStartContext's own
     * `environment` by app_manager_start_internal(), then mutable at runtime via app_env_*()
     * (app/env.h). Protected by AppLedger::mutex, same as every other field here. */
    std::vector<std::string> env {};
};

/** A registered installed package - see app_manager_add_package() (app/manager.h). */
struct AppPackageRecord {
    struct PackageManifest package;
    std::vector<std::string> app_ids;
};

struct AppLedger {
    std::unordered_map<std::string, const AppManifest*> manifests;
    // OptExternalAllocator: bigger entries than `manifests`, and unlike `instances` isn't on the app start/stop hot path.
    std::unordered_map<std::string, AppPackageRecord, std::hash<std::string>, std::equal_to<std::string>, tt::OptExternalAllocator<std::pair<const std::string, AppPackageRecord>>> packages;
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
