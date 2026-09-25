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

/** An app instance's environment, as "NAME=VALUE" strings. OptExternalAllocator since it's
 * unbounded (apps can add entries at runtime via app_env_*()), shared by AppInstanceRecord::env
 * and app_env_apply()/env_internal.h. */
using AppEnv = std::vector<std::string, tt::OptExternalAllocator<std::string>>;

/** A registered/running app instance, as tracked internally by app-module. */
struct AppInstanceRecord {
    uint32_t id;
    /** Empty for a manifest-less app_execute() instance. Not a pointer into the ledger:
     * an external app's manifest can be freed while this instance keeps running. */
    std::string manifestId;
    AppInstanceState state;
    /** Task currently running AppLoaderApi::run() for this instance; NULL when not running. */
    TaskHandle_t task;

    /** 0 for a top-level launch. Non-zero for a modal child: The instance that receives its
     * APP_EVENT_RESULT (see app_start_for_result()). */
    uint32_t parent_id = 0;

    /** See AppCompletionSignal. Set once by app_scheduler_start(), never reassigned. */
    AppCompletionSignal* completion = nullptr;

    /** Constructed before insertion into AppLedger::instances, torn down on task exit. */
    AppFdTable fd_table {};

    /** "NAME=VALUE" strings, seeded from AppStartContext::environment, mutable via app_env_*()
     * (app/env.h). */
    AppEnv env {};

    /** Inherited from the parent instance, like `env`. Mutable via app_dir_set_cwd() (app/dir.h).
     * Always absolute. */
    std::string cwd = "/";
};

/** A registered installed package - see app_manager_add_package() (app/manager.h). */
struct AppPackageRecord {
    struct PackageManifest package;
    // OptExternalAllocator: matches AppLedger::packages, not on the app start/stop hot path.
    std::vector<std::string, tt::OptExternalAllocator<std::string>> app_ids;
};

struct AppLedger {
    std::unordered_map<std::string, const AppManifest*> manifests;
    std::unordered_map<std::string, AppPackageRecord, std::hash<std::string>, std::equal_to<std::string>, tt::OptExternalAllocator<std::pair<const std::string, AppPackageRecord>>> packages;
    std::unordered_map<uint32_t, AppInstanceRecord, std::hash<uint32_t>, std::equal_to<uint32_t>, tt::OptExternalAllocator<std::pair<const uint32_t, AppInstanceRecord>>> instances;
    uint32_t next_instance_id = 1;
    Mutex mutex {};

    AppLedger() { mutex_construct(&mutex); }
    ~AppLedger() { mutex_destruct(&mutex); }
};

inline AppLedger& app_ledger() {
    static AppLedger ledger;
    return ledger;
}
