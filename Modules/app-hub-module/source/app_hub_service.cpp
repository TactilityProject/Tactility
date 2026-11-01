// SPDX-License-Identifier: Apache-2.0
#include <app_hub/app_hub.h>
#include <app_hub/app_hub_catalog.h>
#include <app_hub/app_hub_service.h>

#include <app/install.h>
#include <app/manager.h>
#include <app/manifest.h>

#include <http/download.h>

#include <service/paths.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <tactility/std/deque.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <algorithm>
#include <new>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr auto* TAG = "app_hub";
constexpr auto* CATALOG_FILE_NAME = "catalog.txt";
constexpr auto* CATALOG_TEMP_FILE_NAME = "catalog.txt.tmp";
constexpr auto* CATALOG_DOWNLOAD_FILE_NAME = "apps.json.tmp";
// Older versions kept the downloaded apps.json as the catalog
constexpr auto* LEGACY_CATALOG_FILE_NAME = "apps.json";
constexpr size_t WORKER_STACK_SIZE = 8192;
constexpr TickType_t WORKER_IDLE_TIMEOUT = pdMS_TO_TICKS(5000);

struct Operation {
    uint32_t id;
    AppHubOperationType type;
    PackageId app_id;
};

struct InstalledPackage {
    PackageManifest package;
    AppId first_app_id;
};

using AppList = tt::std::vector<AppHubApp>;

struct GlobalMutex {
    Mutex handle {};
    GlobalMutex() { mutex_construct(&handle); }
    ~GlobalMutex() { mutex_destruct(&handle); }
};

GlobalMutex globalMutex;

void lock() { mutex_lock(&globalMutex.handle); }
void unlock() { mutex_unlock(&globalMutex.handle); }

// region State, guarded by the global lock

struct State {
    tt::std::deque<Operation> queue;
    Operation runningOperation {};
    bool operationRunning = false;
    uint32_t nextOperationId = 1;
    AppHubEventSubscription* subscriptions = nullptr;

    bool serviceRunning = false;
    bool stopping = false;
    Thread* worker = nullptr;
    bool workerRunning = false;
    TaskEventGroup workerEvents {};
    uint32_t queueBit = 0;
    uint32_t stopBit = 0;
};

constexpr MemoryPolicy STATE_MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = alignof(State) };

/** Exists while the service does */
State* state = nullptr;

/** Locks, and unlocks again when the service doesn't exist */
bool lockState() {
    lock();
    if (state != nullptr) {
        return true;
    }
    unlock();
    return false;
}

// endregion

// region Events

AppHubEventType getStartedEventType(AppHubOperationType operation) {
    switch (operation) {
        case APP_HUB_OPERATION_REFRESH: return APP_HUB_EVENT_REFRESHING;
        case APP_HUB_OPERATION_INSTALL: return APP_HUB_EVENT_INSTALLING;
        case APP_HUB_OPERATION_REMOVE: return APP_HUB_EVENT_REMOVING;
        case APP_HUB_OPERATION_UPDATE_ALL: return APP_HUB_EVENT_UPDATING;
    }
    return APP_HUB_EVENT_REFRESHING;
}

AppHubEventType getFinishedEventType(AppHubOperationType operation) {
    switch (operation) {
        case APP_HUB_OPERATION_REFRESH: return APP_HUB_EVENT_REFRESHED;
        case APP_HUB_OPERATION_INSTALL: return APP_HUB_EVENT_INSTALLED;
        case APP_HUB_OPERATION_REMOVE: return APP_HUB_EVENT_REMOVED;
        case APP_HUB_OPERATION_UPDATE_ALL: return APP_HUB_EVENT_UPDATED;
    }
    return APP_HUB_EVENT_REFRESHED;
}

/**
 * Requires the global lock
 * @param[in] appId the app the event is about, which an update of all apps reports per app
 */
void emitEvent(AppHubEventType type, const Operation& operation, const char* appId, error_t result) {
    AppHubEvent event {};
    event.type = type;
    event.operation_id = operation.id;
    strcpy(event.app_id, appId);
    event.result = result;
    for (auto* sub = state->subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.count >= APP_HUB_EVENT_QUEUE_CAPACITY) {
            LOG_W(TAG, "A subscriber's event queue is full: dropped an event of operation %lu", static_cast<unsigned long>(event.operation_id));
            continue;
        }
        const uint8_t tail = (sub->internal.head + sub->internal.count) % APP_HUB_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = event;
        sub->internal.count++;
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
}

// endregion

// region Catalog

bool getCatalogPath(const char* fileName, char* path, size_t pathSize) {
    return service_paths_get_user_data_path(APP_HUB_SERVICE_ID, fileName, path, pathSize) == ERROR_NONE;
}

/**
 * Converts the apps.json of an older version into a catalog file
 * @return false when there is no catalog
 */
bool migrateLegacyCatalog(const char* catalogPath) {
    char legacyPath[FILE_MAX_PATH_STRING_LENGTH];
    if (!getCatalogPath(LEGACY_CATALOG_FILE_NAME, legacyPath, sizeof(legacyPath)) || !path_exists(legacyPath)) {
        return false;
    }
    const bool converted = app_hub_catalog_convert(legacyPath, catalogPath);
    file_remove(legacyPath);
    return converted;
}

/**
 * @param[out] path the path of the catalog file
 * @return false when there is no catalog
 */
bool getCatalogFile(char* path, size_t pathSize) {
    return getCatalogPath(CATALOG_FILE_NAME, path, pathSize) && (path_exists(path) || migrateLegacyCatalog(path));
}

/** Requires the global lock */
bool findCatalogEntry(const char* id, AppHubCatalogEntry& entry) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    return getCatalogFile(path, sizeof(path)) && app_hub_catalog_find(path, id, entry);
}

// endregion

// region Apps

void copyString(char* target, size_t targetSize, const char* source) {
    strncpy(target, source, targetSize - 1);
    target[targetSize - 1] = '\0';
}

/** Copies the installed packages, as app_manager_for_each_package() holds the app manager's lock */
tt::std::vector<InstalledPackage> getInstalledPackages() {
    tt::std::vector<InstalledPackage> packages;
    app_manager_for_each_package([](const AppPackage* package, void* context) {
        auto& packages = *static_cast<tt::std::vector<InstalledPackage>*>(context);
        InstalledPackage installed {};
        installed.package = package->package;
        if (package->app_id_count > 0) {
            copyString(installed.first_app_id, sizeof(installed.first_app_id), package->app_ids[0]);
        }
        packages.push_back(installed);
    }, &packages);
    return packages;
}

/**
 * Requires the global lock. Only one refresh can be queued or running at a time.
 * Installs and removes of different apps can be queued together, but an update of all apps excludes them.
 * @return the refresh, or the first install, remove or update of all apps
 */
const Operation* findPendingOperationOfKind(bool refresh) {
    if (state->operationRunning && (state->runningOperation.type == APP_HUB_OPERATION_REFRESH) == refresh) {
        return &state->runningOperation;
    }
    for (const auto& operation : state->queue) {
        if ((operation.type == APP_HUB_OPERATION_REFRESH) == refresh) {
            return &operation;
        }
    }
    return nullptr;
}

/** Requires the global lock */
const Operation* findPendingOperation(const char* id) {
    if (state->operationRunning && state->runningOperation.type != APP_HUB_OPERATION_REFRESH && strcmp(state->runningOperation.app_id, id) == 0) {
        return &state->runningOperation;
    }
    for (const auto& operation : state->queue) {
        if (operation.type != APP_HUB_OPERATION_REFRESH && strcmp(operation.app_id, id) == 0) {
            return &operation;
        }
    }
    return nullptr;
}

/** Requires the global lock */
AppList collectApps(const tt::std::vector<InstalledPackage>& installedPackages) {
    AppList apps;
    char catalogPath[FILE_MAX_PATH_STRING_LENGTH];
    if (getCatalogFile(catalogPath, sizeof(catalogPath))) {
        app_hub_catalog_for_each(catalogPath, [](const AppHubCatalogEntry& entry, void* context) {
            if (app_hub_catalog_is_compatible(entry)) {
                AppHubApp app {};
                copyString(app.id, sizeof(app.id), entry.id.c_str());
                copyString(app.name, sizeof(app.name), entry.name.c_str());
                copyString(app.version_name, sizeof(app.version_name), entry.version_name.c_str());
                app.version_code = static_cast<uint64_t>(entry.version_code);
                app.in_catalog = true;
                static_cast<AppList*>(context)->push_back(app);
            }
            return true;
        }, &apps);
    }

    for (const auto& installed : installedPackages) {
        auto existing = std::ranges::find_if(apps, [&installed](const AppHubApp& app) {
            return strcmp(app.id, installed.package.id) == 0;
        });
        AppHubApp* app;
        if (existing != apps.end()) {
            app = &*existing;
        } else {
            app = &apps.emplace_back();
            copyString(app->id, sizeof(app->id), installed.package.id);
            AppManifest manifest;
            const bool hasName = installed.first_app_id[0] != '\0' && app_manager_find_manifest(installed.first_app_id, &manifest) == ERROR_NONE;
            copyString(app->name, sizeof(app->name), hasName ? manifest.name : installed.package.id);
        }
        app->installed = true;
        copyString(app->installed_version_name, sizeof(app->installed_version_name), installed.package.version_name);
        app->installed_version_code = installed.package.version_code;
        app->update_available = app->in_catalog && app->installed_version_code < app->version_code;
    }

    const Operation* pendingAppOperation = findPendingOperationOfKind(false);
    const bool updatingAll = pendingAppOperation != nullptr && pendingAppOperation->type == APP_HUB_OPERATION_UPDATE_ALL;
    for (auto& app : apps) {
        const Operation* pending = findPendingOperation(app.id);
        if (pending == nullptr && updatingAll && app.update_available) {
            pending = pendingAppOperation;
        }
        app.operation_pending = pending != nullptr;
        if (pending != nullptr) {
            app.pending_operation = pending->type;
        }
    }
    return apps;
}

bool containsIgnoringCase(const char* text, const char* part) {
    const std::string_view textView(text);
    const std::string_view partView(part);
    return std::ranges::search(textView, partView, [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) == std::tolower(static_cast<unsigned char>(right));
    }).begin() != textView.end() || partView.empty();
}

bool lessIgnoringCase(const AppHubApp& left, const AppHubApp& right) {
    return std::ranges::lexicographical_compare(std::string_view(left.name), std::string_view(right.name), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) < std::tolower(static_cast<unsigned char>(b));
    });
}

// endregion

// region Operations

error_t download(const char* url, const char* targetPath) {
    HttpDownloadSubscription sub {};
    if (http_download_subscribe(&sub, &state->workerEvents) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }
    if (http_download_start(url, app_hub_catalog_get_certificate(), targetPath, &sub) != ERROR_NONE) {
        http_download_unsubscribe(&sub);
        return ERROR_RESOURCE;
    }

    while (true) {
        uint32_t flags = 0;
        task_event_group_wait(&state->workerEvents, sub.bit | state->stopBit, false, &flags, portMAX_DELAY);
        if (flags & state->stopBit) {
            http_download_cancel(&sub);
            http_download_unsubscribe(&sub);
            return ERROR_INVALID_STATE;
        }
        HttpDownloadEvent event {};
        if (http_download_poll(&sub, &event) == ERROR_NONE) {
            http_download_unsubscribe(&sub);
            if (event.type != HTTP_DOWNLOAD_EVENT_SUCCESS) {
                LOG_E(TAG, "Download of %s failed (status %d)", url, event.status_code);
                return ERROR_RESOURCE;
            }
            return ERROR_NONE;
        }
    }
}

error_t ensureDirectory(const char* path) {
    const error_t error = directory_make(path, true);
    return (error == ERROR_ALREADY_EXISTS) ? ERROR_NONE : error;
}

error_t executeRefresh() {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char downloadPath[FILE_MAX_PATH_STRING_LENGTH];
    char catalogPath[FILE_MAX_PATH_STRING_LENGTH];
    char tempCatalogPath[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(APP_HUB_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        !getCatalogPath(CATALOG_DOWNLOAD_FILE_NAME, downloadPath, sizeof(downloadPath)) ||
        !getCatalogPath(CATALOG_FILE_NAME, catalogPath, sizeof(catalogPath)) ||
        !getCatalogPath(CATALOG_TEMP_FILE_NAME, tempCatalogPath, sizeof(tempCatalogPath))) {
        return ERROR_NOT_FOUND;
    }
    error_t error = ensureDirectory(directory);
    if (error != ERROR_NONE) {
        return error;
    }

    error = download(app_hub_catalog_get_url().c_str(), downloadPath);
    if (error != ERROR_NONE) {
        file_remove(downloadPath);
        return error;
    }

    // The catalog file is only replaced by a complete conversion
    const bool converted = app_hub_catalog_convert(downloadPath, tempCatalogPath);
    file_remove(downloadPath);
    if (!converted) {
        return ERROR_INVALID_ARGUMENT;
    }
    lock();
    error = file_move(tempCatalogPath, catalogPath, true);
    unlock();
    return error;
}

error_t executeInstall(const char* id) {
    AppHubCatalogEntry entry;
    lock();
    const bool found = findCatalogEntry(id, entry);
    unlock();
    if (!found) {
        return ERROR_NOT_FOUND;
    }
    const std::string& file = entry.file;
    if (!app_hub_catalog_is_compatible(entry)) {
        return ERROR_NOT_SUPPORTED;
    }

    char tempDirectory[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = paths_get_temp_path(tempDirectory, sizeof(tempDirectory));
    if (error != ERROR_NONE) {
        return error;
    }
    error = ensureDirectory(tempDirectory);
    if (error != ERROR_NONE) {
        return error;
    }
    const auto separator = file.find_last_of('/');
    const tt::std::string fileName = (separator == std::string::npos) ? tt::std::string(file.c_str()) : tt::std::string(file.c_str() + separator + 1);
    const tt::std::string tempPath = tt::std::string(tempDirectory) + "/" + fileName;

    error = download(app_hub_catalog_get_download_url(file).c_str(), tempPath.c_str());
    if (error == ERROR_NONE) {
        error = app_install(tempPath.c_str());
        if (error != ERROR_NONE) {
            LOG_E(TAG, "Install of %s failed: %s", id, error_to_string(error));
        }
    }
    if (path_exists(tempPath.c_str()) && file_remove(tempPath.c_str()) != ERROR_NONE) {
        LOG_W(TAG, "Failed to remove %s", tempPath.c_str());
    }
    return error;
}

error_t executeUpdateAll(const Operation& operation) {
    const auto installed = getInstalledPackages();
    tt::std::vector<tt::std::string> ids;
    lock();
    for (const auto& app : collectApps(installed)) {
        if (app.update_available) {
            ids.emplace_back(app.id);
        }
    }
    unlock();

    error_t lastError = ERROR_NONE;
    for (const auto& id : ids) {
        lock();
        const bool stop = state->stopping;
        if (!stop) {
            emitEvent(APP_HUB_EVENT_INSTALLING, operation, id.c_str(), ERROR_NONE);
        }
        unlock();
        if (stop) {
            return ERROR_INVALID_STATE;
        }

        const error_t error = executeInstall(id.c_str());
        if (error != ERROR_NONE) {
            lastError = error;
        }

        lock();
        emitEvent(APP_HUB_EVENT_INSTALLED, operation, id.c_str(), error);
        unlock();
    }
    return lastError;
}

error_t execute(const Operation& operation) {
    switch (operation.type) {
        case APP_HUB_OPERATION_REFRESH:
            return executeRefresh();
        case APP_HUB_OPERATION_INSTALL:
            return executeInstall(operation.app_id);
        case APP_HUB_OPERATION_REMOVE:
            return app_uninstall(operation.app_id);
        case APP_HUB_OPERATION_UPDATE_ALL:
            return executeUpdateAll(operation);
    }
    return ERROR_INVALID_ARGUMENT;
}

int32_t workerMain(void* /*context*/) {
    while (true) {
        lock();
        if (state->stopping) {
            state->workerRunning = false;
            unlock();
            return 0;
        }
        if (state->queue.empty()) {
            unlock();
            uint32_t flags = 0;
            const error_t waited = task_event_group_wait(&state->workerEvents, state->queueBit | state->stopBit, false, &flags, WORKER_IDLE_TIMEOUT);
            if (waited == ERROR_TIMEOUT) {
                // An operation can be queued between the wait and taking the lock
                lock();
                const bool idle = state->queue.empty();
                if (idle) {
                    state->workerRunning = false;
                }
                unlock();
                if (idle) {
                    return 0;
                }
            }
            continue;
        }

        state->runningOperation = state->queue.front();
        state->queue.pop_front();
        state->operationRunning = true;
        const Operation operation = state->runningOperation;
        emitEvent(getStartedEventType(operation.type), operation, operation.app_id, ERROR_NONE);
        unlock();

        const error_t result = execute(operation);

        lock();
        state->operationRunning = false;
        emitEvent(getFinishedEventType(operation.type), operation, operation.app_id, result);
        unlock();
    }
}

/** Requires the global lock */
error_t ensureWorkerRunning() {
    if (state->workerRunning) {
        return ERROR_NONE;
    }
    if (state->worker != nullptr) {
        // The previous worker cleared workerRunning and exits without taking the lock
        thread_join(state->worker, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(state->worker);
    }
    state->worker = thread_alloc_full("apphub", WORKER_STACK_SIZE, workerMain, nullptr, -1);
    if (state->worker == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    if (thread_start(state->worker) != ERROR_NONE) {
        thread_free(state->worker);
        state->worker = nullptr;
        return ERROR_RESOURCE;
    }
    state->workerRunning = true;
    return ERROR_NONE;
}

error_t enqueue(AppHubOperationType type, const char* id, uint32_t* operationId) {
    if (id != nullptr && (id[0] == '\0' || strlen(id) >= sizeof(PackageId))) {
        return ERROR_INVALID_ARGUMENT;
    }

    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    if (!state->serviceRunning) {
        unlock();
        return ERROR_INVALID_STATE;
    }

    const Operation* pending;
    if (type == APP_HUB_OPERATION_REFRESH) {
        pending = findPendingOperationOfKind(true);
    } else {
        pending = findPendingOperationOfKind(false);
        // Installs and removes only conflict with an operation for the same app, or with an update of all apps
        if (type != APP_HUB_OPERATION_UPDATE_ALL && (pending == nullptr || pending->type != APP_HUB_OPERATION_UPDATE_ALL)) {
            pending = findPendingOperation(id);
        }
    }
    if (pending != nullptr) {
        const bool same = pending->type == type && (id == nullptr || strcmp(pending->app_id, id) == 0);
        if (same && operationId != nullptr) {
            *operationId = pending->id;
        }
        unlock();
        return same ? ERROR_NONE : ERROR_RESOURCE_BUSY;
    }

    Operation operation {};
    operation.id = state->nextOperationId++;
    operation.type = type;
    if (id != nullptr) {
        strcpy(operation.app_id, id);
    }

    const error_t error = ensureWorkerRunning();
    if (error == ERROR_NONE) {
        state->queue.push_back(operation);
        task_event_group_signal(&state->workerEvents, state->queueBit);
        if (operationId != nullptr) {
            *operationId = operation.id;
        }
    }
    unlock();
    return error;
}

// endregion

// region Service

void* createService(const ServiceManifest* /*manifest*/) {
    void* memory = memory_alloc_with_policy(sizeof(State), &STATE_MEMORY_POLICY);
    if (memory == nullptr) {
        return nullptr;
    }
    auto* created = new (memory) State();
    lock();
    state = created;
    unlock();
    return created;
}

void destroyService(const ServiceManifest* /*manifest*/, void* data) {
    if (data == nullptr) {
        return;
    }
    auto* destroyed = static_cast<State*>(data);
    lock();
    state = nullptr;
    unlock();
    destroyed->~State();
    memory_free(destroyed);
}

error_t onStart(ServiceInstance* /*instance*/, void* data) {
    if (data == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    task_event_group_construct(&state->workerEvents);
    if (task_event_group_claim_bit(&state->workerEvents, &state->queueBit) != ERROR_NONE ||
        task_event_group_claim_bit(&state->workerEvents, &state->stopBit) != ERROR_NONE) {
        task_event_group_destruct(&state->workerEvents);
        return ERROR_RESOURCE;
    }
    lock();
    state->serviceRunning = true;
    state->stopping = false;
    unlock();
    return ERROR_NONE;
}

void onStop(ServiceInstance* /*instance*/, void* /*data*/) {
    lock();
    state->serviceRunning = false;
    state->stopping = true;
    state->queue.clear();
    Thread* stoppingWorker = state->worker;
    state->worker = nullptr;
    unlock();

    if (stoppingWorker != nullptr) {
        task_event_group_signal(&state->workerEvents, state->stopBit);
        thread_join(stoppingWorker, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(stoppingWorker);
    }

    lock();
    state->workerRunning = false;
    unlock();
    task_event_group_destruct(&state->workerEvents);
}

// endregion

} // namespace

const ServiceManifest app_hub_service_manifest = {
    .id = APP_HUB_SERVICE_ID,
    .create_service = createService,
    .destroy_service = destroyService,
    .on_start = onStart,
    .on_stop = onStop,
};

bool app_hub_is_worker_running() {
    if (!lockState()) {
        return false;
    }
    const bool running = state->workerRunning;
    unlock();
    return running;
}

extern "C" {

// region Apps

error_t app_hub_search(const char* query, AppHubAppVisitorFn visitor, void* context) {
    const auto installed = getInstalledPackages();
    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    AppList apps = collectApps(installed);
    std::ranges::sort(apps, lessIgnoringCase);
    for (const auto& app : apps) {
        if (query != nullptr && !containsIgnoringCase(app.name, query) && !containsIgnoringCase(app.id, query)) {
            continue;
        }
        if (!visitor(&app, context)) {
            break;
        }
    }
    unlock();
    return ERROR_NONE;
}

error_t app_hub_get_app(const char* id, AppHubApp* app) {
    const auto installed = getInstalledPackages();
    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    const AppList apps = collectApps(installed);
    unlock();
    const auto found = std::ranges::find_if(apps, [id](const AppHubApp& candidate) {
        return strcmp(candidate.id, id) == 0;
    });
    if (found == apps.end()) {
        return ERROR_NOT_FOUND;
    }
    *app = *found;
    return ERROR_NONE;
}

error_t app_hub_get_description(const char* id, char* description, size_t description_size) {
    AppHubCatalogEntry entry;
    lock();
    const bool found = findCatalogEntry(id, entry);
    unlock();
    if (!found) {
        return ERROR_NOT_FOUND;
    }
    if (entry.description.size() >= description_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(description, entry.description.c_str());
    return ERROR_NONE;
}

// endregion

// region Operations

bool app_hub_is_busy() {
    if (!lockState()) {
        return false;
    }
    const bool busy = state->operationRunning || !state->queue.empty();
    unlock();
    return busy;
}

error_t app_hub_refresh(uint32_t* operation_id) {
    return enqueue(APP_HUB_OPERATION_REFRESH, nullptr, operation_id);
}

error_t app_hub_install(const char* id, uint32_t* operation_id) {
    return enqueue(APP_HUB_OPERATION_INSTALL, id, operation_id);
}

error_t app_hub_update_all(uint32_t* operation_id) {
    return enqueue(APP_HUB_OPERATION_UPDATE_ALL, nullptr, operation_id);
}

error_t app_hub_remove(const char* id, uint32_t* operation_id) {
    return enqueue(APP_HUB_OPERATION_REMOVE, id, operation_id);
}

// endregion

// region Events

error_t app_hub_event_subscribe(AppHubEventSubscription* sub, TaskEventGroup* event_group) {
    uint32_t bit;
    const error_t claimed = task_event_group_claim_bit(event_group, &bit);
    if (claimed != ERROR_NONE) {
        return claimed;
    }

    if (!lockState()) {
        task_event_group_release_bit(event_group, bit);
        return ERROR_INVALID_STATE;
    }
    for (auto* existing = state->subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            unlock();
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }
    sub->bit = bit;
    sub->internal.event_group = event_group;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = state->subscriptions;
    state->subscriptions = sub;
    unlock();
    return ERROR_NONE;
}

error_t app_hub_event_unsubscribe(AppHubEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;
    if (!lockState()) {
        return result;
    }
    for (auto** link = &state->subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    unlock();
    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }
    return result;
}

error_t app_hub_event_await(AppHubEventSubscription* sub, TickType_t timeout) {
    const TickType_t start = get_ticks();
    while (true) {
        lock();
        const bool hasEvent = sub->internal.count > 0;
        TaskEventGroup* eventGroup = sub->internal.event_group;
        unlock();
        if (hasEvent) {
            return ERROR_NONE;
        }
        const TickType_t elapsed = get_ticks() - start;
        if (elapsed >= timeout) {
            return ERROR_TIMEOUT;
        }
        // The bit can be left over from an event that was already polled, so the queue is checked again
        task_event_group_wait(eventGroup, sub->bit, false, nullptr, timeout - elapsed);
    }
}

error_t app_hub_event_poll(AppHubEventSubscription* sub, AppHubEvent* event) {
    lock();
    const bool hasEvent = sub->internal.count > 0;
    if (hasEvent) {
        *event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % APP_HUB_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    unlock();
    return hasEvent ? ERROR_NONE : ERROR_TIMEOUT;
}

// endregion

}
