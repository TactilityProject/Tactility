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
#include <atomic>
#include <new>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace {

constexpr auto* TAG = "app_hub";
constexpr auto* CATALOG_FILE_NAME = "index.txt";
constexpr auto* REPOSITORIES_FILE_NAME = "repositories.txt";
constexpr auto* CATALOG_TEMP_FILE_NAME = "index.txt.tmp";
constexpr auto* CATALOG_DOWNLOAD_FILE_NAME = "index.json.tmp";
constexpr auto* ICONS_DIRECTORY_NAME = "icons";
// Guards against a list with an absurd page count
constexpr int CATALOG_MAX_PAGES = 100;
constexpr size_t WORKER_STACK_SIZE = 8192;
constexpr TickType_t WORKER_IDLE_TIMEOUT = pdMS_TO_TICKS(5000);

struct Operation {
    uint32_t id;
    AppHubOperationType type;
    PackageId app_id;
};

struct InstalledPackage {
    PackageSummary package;
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
    /** Changes with the list of repositories, so a refresh that started before a change is redone */
    uint32_t repositoriesGeneration = 0;
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
 * @param[out] path the path of the catalog file
 * @return false when there is no catalog
 */
bool getCatalogFile(char* path, size_t pathSize) {
    return getCatalogPath(CATALOG_FILE_NAME, path, pathSize) && path_exists(path);
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
            AppHubApp app {};
            copyString(app.id, sizeof(app.id), entry.id.c_str());
            copyString(app.name, sizeof(app.name), entry.name.c_str());
            copyString(app.short_description, sizeof(app.short_description), entry.short_description.c_str());
            app.version_code = static_cast<uint64_t>(entry.version_code);
            app.has_icon = entry.has_icon;
            app.in_catalog = true;
            static_cast<AppList*>(context)->push_back(app);
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

/**
 * @param[in] certificate the PEM text that verifies the server, or an empty string for the trusted roots of the platform
 * @param[in] events the event group to wait on
 * @param[in] stopBit a bit of @a events that cancels the download, or 0
 */
error_t download(const char* url, const char* certificate, const char* targetPath, TaskEventGroup* events, uint32_t stopBit) {
    HttpDownloadSubscription sub {};
    if (http_download_subscribe(&sub, events) != ERROR_NONE) {
        return ERROR_RESOURCE;
    }
    if (http_download_start(url, certificate, targetPath, &sub) != ERROR_NONE) {
        http_download_unsubscribe(&sub);
        return ERROR_RESOURCE;
    }

    while (true) {
        uint32_t flags = 0;
        task_event_group_wait(events, sub.bit | stopBit, false, &flags, portMAX_DELAY);
        if (flags & stopBit) {
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

// region Repositories

struct Repository {
    tt::std::string url;
    bool enabled = true;
    bool official = false;
};

using RepositoryList = tt::std::vector<Repository>;

/** Where a catalog app is downloaded from */
struct Source {
    tt::std::string url;
    bool official = false;
};

const char* getCertificate(bool official) {
    return official ? app_hub_catalog_get_certificate() : "";
}

/**
 * Requires the global lock. The official repository is first, followed by the others in the order of the file.
 * A line of the file is "<1 or 0 for enabled>\t<url>", the official repository has an empty url.
 */
RepositoryList loadRepositories() {
    RepositoryList repositories;
    repositories.push_back({ tt::std::string(app_hub_catalog_get_official_url()), true, true });
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getCatalogPath(REPOSITORIES_FILE_NAME, path, sizeof(path))) {
        return repositories;
    }
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return repositories;
    }
    char line[APP_HUB_REPOSITORY_URL_MAX_LENGTH + 8];
    while (fgets(line, sizeof(line), file) != nullptr) {
        line[strcspn(line, "\r\n")] = '\0';
        if ((line[0] != '0' && line[0] != '1') || line[1] != '\t') {
            continue;
        }
        const bool enabled = line[0] == '1';
        const char* url = line + 2;
        if (url[0] == '\0') {
            repositories[0].enabled = enabled;
        } else if (repositories.size() < APP_HUB_REPOSITORY_MAX_COUNT) {
            repositories.push_back({ tt::std::string(url), enabled, false });
        }
    }
    fclose(file);
    return repositories;
}

/** Requires the global lock */
bool saveRepositories(const RepositoryList& repositories) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(APP_HUB_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        !getCatalogPath(REPOSITORIES_FILE_NAME, path, sizeof(path)) ||
        ensureDirectory(directory) != ERROR_NONE) {
        return false;
    }
    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    bool success = true;
    for (const auto& repository : repositories) {
        success &= fprintf(file, "%c\t%s\n", repository.enabled ? '1' : '0', repository.official ? "" : repository.url.c_str()) > 0;
    }
    return (fclose(file) == 0) && success;
}

/**
 * The entries of the catalog have the index of their repository, so the catalog is discarded when the repositories change.
 * Requires the global lock.
 */
void discardCatalog() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (getCatalogFile(path, sizeof(path))) {
        file_remove(path);
    }
    state->repositoriesGeneration++;
}

/** @return false when the app's repository doesn't exist anymore */
bool getSource(int32_t repositoryIndex, Source& source) {
    lock();
    const RepositoryList repositories = loadRepositories();
    unlock();
    if (repositoryIndex < 0 || static_cast<size_t>(repositoryIndex) >= repositories.size()) {
        return false;
    }
    source.url = repositories[repositoryIndex].url;
    source.official = repositories[repositoryIndex].official;
    return true;
}

/** @return false when the text is not an HTTPS URL that can be stored */
bool normalizeUrl(const char* input, tt::std::string& url) {
    std::string_view text(input);
    while (!text.empty() && (text.front() == ' ' || text.back() == ' ' || text.back() == '/')) {
        if (text.front() == ' ') {
            text.remove_prefix(1);
        } else {
            text.remove_suffix(1);
        }
    }
    constexpr std::string_view SCHEME = "https://";
    if (text.size() <= SCHEME.size() || text.size() > APP_HUB_REPOSITORY_URL_MAX_LENGTH || !text.starts_with(SCHEME)) {
        return false;
    }
    for (const char character : text) {
        if (static_cast<unsigned char>(character) <= ' ' || character == 0x7f) {
            return false;
        }
    }
    url = tt::std::string(std::string(text).c_str());
    return true;
}

/**
 * Applies a change to the list, saves it and refreshes the catalog.
 * @param[in] change changes the list and sets its second parameter when there is something to save
 */
template <typename Change>
error_t changeRepositories(Change change) {
    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    RepositoryList repositories = loadRepositories();
    bool changed = false;
    error_t error = change(repositories, changed);
    if (error == ERROR_NONE && changed) {
        if (saveRepositories(repositories)) {
            discardCatalog();
        } else {
            error = ERROR_RESOURCE;
        }
    }
    unlock();
    if (error == ERROR_NONE && changed) {
        app_hub_refresh(nullptr);
    }
    return error;
}

Repository* findRepository(RepositoryList& repositories, const char* url) {
    tt::std::string normalized;
    if (!normalizeUrl(url, normalized)) {
        return nullptr;
    }
    for (auto& repository : repositories) {
        if (repository.url == normalized) {
            return &repository;
        }
    }
    return nullptr;
}

// endregion

/**
 * Downloads the list of a repository and appends its apps to the catalog file, except the ones of earlier repositories
 * @param[in,out] knownIds the ids of the apps of the earlier repositories, which get the ids of this repository
 */
error_t refreshRepository(const Repository& repository, int32_t index, tt::std::vector<tt::std::string>& knownIds, const char* downloadPath, const char* tempCatalogPath) {
    struct Filter {
        const tt::std::vector<tt::std::string>& known;
        tt::std::vector<tt::std::string> added;
    } filter { knownIds, {} };
    int32_t pageCount = 1;
    for (int32_t page = 1; page <= pageCount; page++) {
        const std::string url = app_hub_catalog_get_page_url(repository.url.c_str(), page);
        const error_t error = download(url.c_str(), getCertificate(repository.official), downloadPath, &state->workerEvents, state->stopBit);
        if (error != ERROR_NONE) {
            return error;
        }
        const bool appended = app_hub_catalog_append_page(downloadPath, tempCatalogPath, pageCount, index, [](const AppHubCatalogEntry& entry, void* context) {
            auto* filter = static_cast<Filter*>(context);
            for (const auto& id : filter->known) {
                if (strcmp(id.c_str(), entry.id.c_str()) == 0) {
                    return false;
                }
            }
            filter->added.emplace_back(entry.id.c_str());
            return true;
        }, &filter);
        file_remove(downloadPath);
        if (!appended || pageCount > CATALOG_MAX_PAGES) {
            return ERROR_INVALID_ARGUMENT;
        }
    }
    for (auto& id : filter.added) {
        knownIds.push_back(std::move(id));
    }
    return ERROR_NONE;
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

    while (true) {
        lock();
        const uint32_t generation = state->repositoriesGeneration;
        const RepositoryList repositories = loadRepositories();
        unlock();

        // The catalog file is only replaced by a complete download of the lists of all enabled repositories
        file_remove(tempCatalogPath);
        tt::std::vector<tt::std::string> knownIds;
        for (size_t index = 0; index < repositories.size(); index++) {
            if (!repositories[index].enabled) {
                continue;
            }
            error = refreshRepository(repositories[index], static_cast<int32_t>(index), knownIds, downloadPath, tempCatalogPath);
            if (error != ERROR_NONE) {
                file_remove(downloadPath);
                file_remove(tempCatalogPath);
                return error;
            }
        }
        // Without enabled repositories the catalog is empty, but it exists
        if (FILE* file = fopen(tempCatalogPath, "ab")) {
            fclose(file);
        }

        lock();
        if (state->stopping) {
            unlock();
            file_remove(tempCatalogPath);
            return ERROR_INVALID_STATE;
        }
        // The repositories changed during the refresh, so the lists are downloaded again
        if (generation != state->repositoriesGeneration) {
            unlock();
            continue;
        }
        error = file_move(tempCatalogPath, catalogPath, true);
        unlock();
        return error;
    }
}

/** Downloads and reads the details file of an app from its repository, see download() for the event group */
error_t fetchDetails(const char* id, const Source& source, AppHubCatalogDetails& details, TaskEventGroup* events, uint32_t stopBit) {
    char tempDirectory[FILE_MAX_PATH_STRING_LENGTH];
    error_t error = paths_get_temp_path(tempDirectory, sizeof(tempDirectory));
    if (error != ERROR_NONE) {
        return error;
    }
    error = ensureDirectory(tempDirectory);
    if (error != ERROR_NONE) {
        return error;
    }
    // Every request has its own file, as several can be running for the same app: the install and the details screen
    static std::atomic<uint32_t> nextRequest { 0 };
    const std::string fileName = app_hub_catalog_get_details_file(id);
    const std::string path = std::string(tempDirectory) + "/" + std::to_string(nextRequest++) + "-" + fileName;
    error = download(app_hub_catalog_get_download_url(source.url.c_str(), fileName).c_str(), getCertificate(source.official), path.c_str(), events, stopBit);
    if (error == ERROR_NONE && !app_hub_catalog_read_details(path.c_str(), details)) {
        error = ERROR_INVALID_ARGUMENT;
    }
    if (path_exists(path.c_str())) {
        file_remove(path.c_str());
    }
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
    Source source;
    if (!getSource(entry.repository, source)) {
        return ERROR_NOT_FOUND;
    }
    AppHubCatalogDetails details;
    error_t detailsError = fetchDetails(id, source, details, &state->workerEvents, state->stopBit);
    if (detailsError != ERROR_NONE) {
        return detailsError;
    }
    const std::string& file = details.file;
    if (!app_hub_catalog_is_compatible(details)) {
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

    error = download(app_hub_catalog_get_download_url(source.url.c_str(), file).c_str(), getCertificate(source.official), tempPath.c_str(), &state->workerEvents, state->stopBit);
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

error_t app_hub_get_details(const char* id, char* description, size_t description_size, AppHubAppDetails* app_details) {
    AppHubCatalogEntry entry;
    lock();
    const bool found = findCatalogEntry(id, entry);
    unlock();
    if (!found) {
        return ERROR_NOT_FOUND;
    }
    Source source;
    if (!getSource(entry.repository, source)) {
        return ERROR_NOT_FOUND;
    }
    TaskEventGroup events {};
    task_event_group_construct(&events);
    AppHubCatalogDetails details;
    const error_t error = fetchDetails(id, source, details, &events, 0);
    task_event_group_destruct(&events);
    if (error != ERROR_NONE) {
        return error;
    }
    if (details.description.size() >= description_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(description, details.description.c_str());
    copyString(app_details->version_name, sizeof(app_details->version_name), details.version_name.c_str());
    app_details->compatible = app_hub_catalog_is_compatible(details);
    return ERROR_NONE;
}

error_t app_hub_get_icon(const char* id, bool mono, char* path, size_t path_size) {
    AppHubCatalogEntry entry;
    lock();
    const bool found = findCatalogEntry(id, entry);
    unlock();
    if (!found) {
        return ERROR_NOT_FOUND;
    }
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    if (!getCatalogPath(ICONS_DIRECTORY_NAME, directory, sizeof(directory))) {
        return ERROR_NOT_FOUND;
    }
    Source source;
    if (!getSource(entry.repository, source)) {
        return ERROR_NOT_FOUND;
    }
    error_t error = ensureDirectory(directory);
    if (error != ERROR_NONE) {
        return error;
    }
    // The version is in the file name, so the icon of an older version of the app is never used
    const std::string iconFile = app_hub_catalog_get_icon_file(id, entry.version_code, mono);
    const std::string cachePath = std::string(directory) + "/" + iconFile;
    if (cachePath.size() >= path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    if (!path_exists(cachePath.c_str())) {
        TaskEventGroup events {};
        task_event_group_construct(&events);
        error = download(app_hub_catalog_get_download_url(source.url.c_str(), iconFile).c_str(), getCertificate(source.official), cachePath.c_str(), &events, 0);
        task_event_group_destruct(&events);
        if (error != ERROR_NONE) {
            file_remove(cachePath.c_str());
            return error;
        }
    }
    strcpy(path, cachePath.c_str());
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

// region Repositories

error_t app_hub_repository_for_each(AppHubRepositoryVisitorFn visitor, void* context) {
    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    const RepositoryList repositories = loadRepositories();
    unlock();
    for (const auto& repository : repositories) {
        AppHubRepository result {};
        copyString(result.url, sizeof(result.url), repository.url.c_str());
        result.enabled = repository.enabled;
        result.official = repository.official;
        if (!visitor(&result, context)) {
            break;
        }
    }
    return ERROR_NONE;
}

error_t app_hub_repository_add(const char* url) {
    tt::std::string normalized;
    if (!normalizeUrl(url, normalized)) {
        return ERROR_INVALID_ARGUMENT;
    }
    return changeRepositories([&normalized](RepositoryList& repositories, bool& changed) {
        for (const auto& repository : repositories) {
            if (repository.url == normalized) {
                return ERROR_ALREADY_EXISTS;
            }
        }
        if (repositories.size() >= APP_HUB_REPOSITORY_MAX_COUNT) {
            return ERROR_OUT_OF_RANGE;
        }
        repositories.push_back({ normalized, true, false });
        changed = true;
        return ERROR_NONE;
    });
}

error_t app_hub_repository_remove(const char* url) {
    return changeRepositories([url](RepositoryList& repositories, bool& changed) {
        auto* repository = findRepository(repositories, url);
        if (repository == nullptr) {
            return ERROR_NOT_FOUND;
        }
        if (repository->official) {
            return ERROR_NOT_ALLOWED;
        }
        repositories.erase(repositories.begin() + (repository - repositories.data()));
        changed = true;
        return ERROR_NONE;
    });
}

error_t app_hub_repository_set_enabled(const char* url, bool enabled) {
    return changeRepositories([url, enabled](RepositoryList& repositories, bool& changed) {
        auto* repository = findRepository(repositories, url);
        if (repository == nullptr) {
            return ERROR_NOT_FOUND;
        }
        changed = repository->enabled != enabled;
        repository->enabled = enabled;
        return ERROR_NONE;
    });
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
