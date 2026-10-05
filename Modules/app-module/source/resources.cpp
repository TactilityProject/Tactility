// SPDX-License-Identifier: Apache-2.0
#include <app/resources.h>
#include <app/private/resources.h>
#include <app/private/scheduler.h>
#include <app/scheduler.h>

#include <tactility/delay.h>
#include <tactility/log.h>

#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <new>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

constexpr auto* TAG = "app_resources";

namespace {

constexpr uint32_t TASK_POLL_INTERVAL_MS = 10;

struct Tracker {
    std::unordered_set<void*> allocations;
    std::unordered_set<int> fds;
    std::unordered_set<FILE*> files;
    std::unordered_set<DIR*> dirs;
    struct Task {
        AppResourceTaskDelete delete_task;
        AppResourceTaskJoin join_task;
    };
    std::unordered_map<void*, Task> tasks;
    // Set by app_resources_release_tasks(): no task can start or be created anymore
    bool tasks_released = false;
    size_t deleted_task_count = 0;
};

// std rather than the kernel's Mutex: tasks an app created with pthread_create() aren't FreeRTOS tasks on the simulator.
// Recursive, since app_resources_create_task() holds it while the platform creates a task.
std::recursive_mutex registry_mutex;
std::unordered_map<AppInstanceId, Tracker*> registry;
// Lets every hook return without locking while no app tracks its resources
std::atomic<int> registry_size { 0 };

// Set by app_resources_enter_thread(). Only read once registry_size is non-zero, as thread_local can't be read before the scheduler starts on ESP32.
thread_local AppInstanceId thread_owner = 0;

AppInstanceId current_owner() {
    return thread_owner != 0 ? thread_owner : app_scheduler_current_app_id();
}

/** @return the tracker of @a app_instance_id if it still accepts tasks, nullptr otherwise. Requires registry_mutex. */
Tracker* find_tracker_accepting_tasks(AppInstanceId app_instance_id) {
    auto iterator = registry.find(app_instance_id);
    return iterator != registry.end() && !iterator->second->tasks_released ? iterator->second : nullptr;
}

/** Runs @a action on the calling task's tracker, under registry_mutex. */
template<typename Action>
void with_current_tracker(Action action) {
    if (registry_size.load(std::memory_order_acquire) == 0) {
        return;
    }
    const AppInstanceId app_instance_id = current_owner();
    if (app_instance_id == 0) {
        return;
    }
    std::lock_guard lock(registry_mutex);
    auto iterator = registry.find(app_instance_id);
    if (iterator != registry.end()) {
        action(*iterator->second);
    }
}

size_t task_count(AppInstanceId app_instance_id) {
    std::lock_guard lock(registry_mutex);
    auto iterator = registry.find(app_instance_id);
    return iterator != registry.end() ? iterator->second->tasks.size() : 0;
}

} // namespace

error_t app_resources_register(AppInstanceId app_instance_id) {
    auto* tracker = new (std::nothrow) Tracker();
    if (tracker == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    std::lock_guard lock(registry_mutex);
    registry[app_instance_id] = tracker;
    registry_size.fetch_add(1, std::memory_order_release);
    return ERROR_NONE;
}

void app_resources_release_tasks(AppInstanceId app_instance_id) {
    for (uint32_t waited = 0; waited < APP_CLEANUP_TASK_GRACE_MS && task_count(app_instance_id) > 0; waited += TASK_POLL_INTERVAL_MS) {
        delay_millis(TASK_POLL_INTERVAL_MS);
    }

    std::vector<std::pair<void*, AppResourceTaskJoin>> joins;
    {
        std::lock_guard lock(registry_mutex);
        auto iterator = registry.find(app_instance_id);
        if (iterator == registry.end()) {
            return;
        }
        Tracker* tracker = iterator->second;
        tracker->tasks_released = true;
        // Deleted while holding the lock, so none of them can be stopped halfway through a tracker call
        for (auto& [handle, task] : tracker->tasks) {
            task.delete_task(handle);
            if (task.join_task != nullptr) {
                joins.emplace_back(handle, task.join_task);
            }
        }
        tracker->deleted_task_count = tracker->tasks.size();
        tracker->tasks.clear();
    }

    // Outside the lock: a task that is ending may still make tracker calls
    for (auto& [handle, join_task] : joins) {
        join_task(handle);
    }

    thread_owner = app_instance_id;
}

void app_resources_release(AppInstanceId app_instance_id) {
    Tracker* tracker;
    {
        std::lock_guard lock(registry_mutex);
        auto iterator = registry.find(app_instance_id);
        if (iterator == registry.end()) {
            return;
        }
        tracker = iterator->second;
        registry.erase(iterator);
        registry_size.fetch_sub(1, std::memory_order_release);
    }
    thread_owner = 0;

    for (FILE* file : tracker->files) {
        fclose(file);
    }
    for (DIR* dir : tracker->dirs) {
        closedir(dir);
    }
    for (int fd : tracker->fds) {
        close(fd);
    }
    for (void* ptr : tracker->allocations) {
        free(ptr);
    }

    if (tracker->deleted_task_count != 0 || !tracker->files.empty() || !tracker->dirs.empty() || !tracker->fds.empty() || !tracker->allocations.empty()) {
        LOG_W(TAG, "[instance %lu] Released %u tasks, %u files, %u directories, %u fds and %u allocations",
            static_cast<unsigned long>(app_instance_id),
            static_cast<unsigned>(tracker->deleted_task_count),
            static_cast<unsigned>(tracker->files.size()),
            static_cast<unsigned>(tracker->dirs.size()),
            static_cast<unsigned>(tracker->fds.size()),
            static_cast<unsigned>(tracker->allocations.size()));
    }

    delete tracker;
}

extern "C" {

AppInstanceId app_resources_current_app(void) {
    if (registry_size.load(std::memory_order_acquire) == 0) {
        return 0;
    }
    const AppInstanceId app_instance_id = current_owner();
    if (app_instance_id == 0) {
        return 0;
    }
    std::lock_guard lock(registry_mutex);
    return registry.contains(app_instance_id) ? app_instance_id : 0;
}

bool app_resources_enter_task(AppInstanceId app_instance_id) {
    std::lock_guard lock(registry_mutex);
    if (find_tracker_accepting_tasks(app_instance_id) == nullptr) {
        return false;
    }
    app_scheduler_set_current_app_id(app_instance_id);
    return true;
}

bool app_resources_enter_thread(AppInstanceId app_instance_id) {
    std::lock_guard lock(registry_mutex);
    if (find_tracker_accepting_tasks(app_instance_id) == nullptr) {
        return false;
    }
    thread_owner = app_instance_id;
    return true;
}

void app_resources_track_alloc(void* ptr) {
    with_current_tracker([ptr](Tracker& tracker) { tracker.allocations.insert(ptr); });
}

void app_resources_untrack_alloc(void* ptr) {
    with_current_tracker([ptr](Tracker& tracker) { tracker.allocations.erase(ptr); });
}

void app_resources_track_fd(int fd) {
    with_current_tracker([fd](Tracker& tracker) { tracker.fds.insert(fd); });
}

void app_resources_untrack_fd(int fd) {
    with_current_tracker([fd](Tracker& tracker) { tracker.fds.erase(fd); });
}

void app_resources_track_file(FILE* file) {
    with_current_tracker([file](Tracker& tracker) { tracker.files.insert(file); });
}

void app_resources_untrack_file(FILE* file) {
    with_current_tracker([file](Tracker& tracker) { tracker.files.erase(file); });
}

void app_resources_track_dir(DIR* dir) {
    with_current_tracker([dir](Tracker& tracker) { tracker.dirs.insert(dir); });
}

void app_resources_untrack_dir(DIR* dir) {
    with_current_tracker([dir](Tracker& tracker) { tracker.dirs.erase(dir); });
}

bool app_resources_create_task(AppResourceTaskCreate create, void* context, AppResourceTaskDelete delete_task, AppResourceTaskJoin join_task) {
    std::lock_guard lock(registry_mutex);
    const AppInstanceId app_instance_id = current_owner();
    if (app_instance_id != 0 && registry.contains(app_instance_id) && find_tracker_accepting_tasks(app_instance_id) == nullptr) {
        return false;
    }
    void* handle = nullptr;
    if (!create(context, &handle)) {
        return false;
    }
    with_current_tracker([handle, delete_task, join_task](Tracker& tracker) { tracker.tasks[handle] = { delete_task, join_task }; });
    return true;
}

void app_resources_untrack_task(void* handle) {
    with_current_tracker([handle](Tracker& tracker) { tracker.tasks.erase(handle); });
}

} // extern "C"
