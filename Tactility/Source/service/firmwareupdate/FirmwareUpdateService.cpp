// SPDX-License-Identifier: Apache-2.0
#include <Tactility/service/firmwareupdate/FirmwareUpdate.h>

#include <Tactility/BuildInfo.h>
#include <Tactility/i18n/Localisation.h>
#include <Tactility/network/Ntp.h>

#include <app/manager.h>
#include <app/start.h>
#include <http/client.h>
#include <localisation/tr.h>
#include <notification/notification.h>
#include <service/manifest.h>
#include <service/paths.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/paths.h>
#include <tactility/firmware/firmware.h>
#include <tactility/log.h>
#include <tactility/memory.h>
#include <tactility/preferences.h>
#include <tactility/system_event.h>

#include <cJSON.h>
#include <generated_assets.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>
#include <string>

namespace tt::service::firmwareupdate {

extern const ::ServiceManifest manifest;

namespace {

constexpr auto* TAG = "FirmwareUpdate";
constexpr auto* SERVICE_ID = "tactility.firmwareupdate";
constexpr auto* BASE_URL = "https://cdn.tactilityproject.org/firmware";
constexpr auto* SETTINGS_FILE_NAME = "settings.properties";
constexpr auto* SETTING_LAST_CHECK = "lastCheck";
constexpr auto* SETTING_CHANNEL = "channel";
constexpr auto* SOFTWARE_APP_ID = "tactility.softwaresettings";
constexpr int64_t CHECK_INTERVAL_SECONDS = 10 * 60;
constexpr size_t MANIFEST_MAX_SIZE = 2048;
constexpr size_t DOWNLOAD_CHUNK_SIZE = 4096;
// The TLS handshake runs on the worker
constexpr size_t WORKER_STACK_SIZE = 8192;
constexpr size_t OPEN_APP_STACK_SIZE = 3072;
constexpr size_t SUBSCRIPTION_CAPACITY = 4;

struct Subscription {
    TaskEventGroup* eventGroup;
    uint32_t bit;
};

struct State {
    Snapshot snapshot {};
    bool checkRequested = false;
    bool checkForced = false;
    bool installRequested = false;
    /** The next check treats the published build as an update and installs it */
    bool forceUpdateRequested = false;
    bool stopping = false;
    Thread* worker = nullptr;
    bool workerRunning = false;
    /** The client of the running download, which onStop() cancels */
    HttpClient* client = nullptr;
    uint32_t notificationId = 0;
    /** The file of the release that was found by the last check, relative to the channel's folder */
    char releaseFile[128] {};
    /** Changes with the channel, so the result of a check or install that started before a change is discarded */
    uint32_t channelGeneration = 0;
    /** The commit of the release that the user confirmed, which is the only one that an install request installs */
    char installCommit[sizeof(Release::gitCommit)] {};
    Subscription subscriptions[SUBSCRIPTION_CAPACITY] {};
};

constexpr MemoryPolicy STATE_MEMORY_POLICY = { .required = 0, .desired = MEMORY_CAPABILITY_EXTERNAL, .alignment = alignof(State) };

struct GlobalMutex {
    Mutex handle {};
    GlobalMutex() { mutex_construct(&handle); }
    ~GlobalMutex() { mutex_destruct(&handle); }
};

GlobalMutex globalMutex;

void lock() { mutex_lock(&globalMutex.handle); }
void unlock() { mutex_unlock(&globalMutex.handle); }

/** Serializes the reads and writes of the settings file, which is rewritten as a whole. Never taken together with the global lock. */
GlobalMutex settingsMutex;

/** Exists while the service does, guarded by the global lock */
State* state = nullptr;

bool lockState() {
    lock();
    if (state != nullptr) {
        return true;
    }
    unlock();
    return false;
}

/** Requires the global lock */
void notifySubscribers() {
    for (const auto& subscription : state->subscriptions) {
        if (subscription.eventGroup != nullptr) {
            task_event_group_signal(subscription.eventGroup, subscription.bit);
        }
    }
}

void setStatus(Status status, uint8_t progress = 0, bool downloading = false) {
    lock();
    state->snapshot.status = status;
    state->snapshot.progress = progress;
    state->snapshot.downloading = downloading;
    notifySubscribers();
    unlock();
}

// region Settings

bool getSettingsPath(char* path, size_t pathSize) {
    return service_paths_get_user_data_path(SERVICE_ID, SETTINGS_FILE_NAME, path, pathSize) == ERROR_NONE;
}

/** @return @a fallback when @a name isn't a channel */
Channel parseChannel(const char* name, Channel fallback) {
    if (strcmp(name, "stable") == 0) {
        return Channel::Stable;
    }
    if (strcmp(name, "unstable") == 0) {
        return Channel::Unstable;
    }
    if (strcmp(name, "develop") == 0) {
        return Channel::Develop;
    }
    return fallback;
}

/** The channel that this firmware was published to */
Channel getBuildChannel() {
    return parseChannel(tt_build_channel, Channel::Develop);
}

/** The channel that the user chose, or else the channel of the build */
Channel loadChannel() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getSettingsPath(path, sizeof(path))) {
        return getBuildChannel();
    }
    mutex_lock(&settingsMutex.handle);
    Preferences* preferences = preferences_open(path);
    char value[16] = "";
    if (preferences != nullptr) {
        preferences_opt_string(preferences, SETTING_CHANNEL, value, sizeof(value));
        preferences_close(preferences);
    }
    mutex_unlock(&settingsMutex.handle);
    return parseChannel(value, getBuildChannel());
}

/** @return 0 when no check was stored */
int64_t loadLastCheck() {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getSettingsPath(path, sizeof(path))) {
        return 0;
    }
    mutex_lock(&settingsMutex.handle);
    Preferences* preferences = preferences_open(path);
    int64_t lastCheck = 0;
    if (preferences != nullptr) {
        preferences_opt_int64(preferences, SETTING_LAST_CHECK, &lastCheck);
        preferences_close(preferences);
    }
    mutex_unlock(&settingsMutex.handle);
    return lastCheck;
}

template <typename Write>
void writeSettings(Write write) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getSettingsPath(path, sizeof(path))) {
        return;
    }
    mutex_lock(&settingsMutex.handle);
    Preferences* preferences = preferences_open(path);
    if (preferences != nullptr) {
        write(preferences);
        preferences_close(preferences);
    } else {
        LOG_E(TAG, "Failed to open %s", path);
    }
    mutex_unlock(&settingsMutex.handle);
}

// endregion

// region Notification

void openAppTask(void* /*parameter*/) {
    AppStartContext context;
    AppInstanceId instance = 0;
    if (!app_manager_is_running(SOFTWARE_APP_ID) && app_start_context_from_id(SOFTWARE_APP_ID, &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance);
    }
    vTaskDelete(nullptr);
}

/** Runs on the task of the UI, so it only starts a task that does the work */
void onNotificationAction(uint32_t /*id*/, void* /*data*/) {
    if (xTaskCreate(openAppTask, "fwupdate_open", OPEN_APP_STACK_SIZE / sizeof(StackType_t), nullptr, tskIDLE_PRIORITY + 2, nullptr) != pdPASS) {
        LOG_E(TAG, "Failed to start the task that shows the Software app");
    }
}

void showNotification(const Release& release) {
    const ::std::string message = i18n::format(tr("Version {} is ready to install"), { release.version });
    NotificationContext context = notification_context_for_service(SERVICE_ID, message.c_str());
    notification_context_set_title(&context, tr("Tactility update available"));
    notification_context_set_action(&context, onNotificationAction, nullptr);
    notification_context_set_sticky(&context, true);
    lock();
    uint32_t id = state->notificationId;
    unlock();
    error_t error = id != 0 ? notification_update(id, &context, false) : ERROR_NOT_FOUND;
    if (error == ERROR_NOT_FOUND) {
        error = notification_send(&context, &id);
    }
    if (error != ERROR_NONE) {
        notification_context_destroy(&context);
        return;
    }
    lock();
    state->notificationId = id;
    unlock();
}

void dismissNotification() {
    lock();
    const uint32_t id = state->notificationId;
    state->notificationId = 0;
    unlock();
    if (id != 0) {
        notification_dismiss(id);
    }
}

// endregion

// region Download

/** The folder of the channel on the CDN */
const char* getChannelPath(Channel channel) {
    switch (channel) {
        case Channel::Stable: return "stable";
        case Channel::Unstable: return "snapshot";
        case Channel::Develop: return "develop";
    }
    return "snapshot";
}

/** The name of the channel in the settings */
const char* getChannelSetting(Channel channel) {
    switch (channel) {
        case Channel::Stable: return "stable";
        case Channel::Unstable: return "unstable";
        case Channel::Develop: return "develop";
    }
    return "unstable";
}

::std::string getUrl(Channel channel, const char* fileName) {
    return ::std::string(BASE_URL) + "/" + getChannelPath(channel) + "/" + fileName;
}

/** Opens @a url with the client of the state, which is created when needed. @return the client, or nullptr on failure. */
HttpClient* openUrl(const ::std::string& url) {
    lock();
    if (state->client == nullptr) {
        state->client = http_client_alloc();
        if (state->client != nullptr) {
            // The trusted roots of the platform don't verify the CDN
            http_client_set_certificate(state->client, reinterpret_cast<const char*>(asset_certificates_GTS_Root_R4_pem));
        }
    }
    HttpClient* client = state->client;
    const bool stopping = state->stopping;
    unlock();
    if (client == nullptr || stopping) {
        return nullptr;
    }
    if (http_client_open(client, url.c_str()) != ERROR_NONE) {
        LOG_E(TAG, "Request for %s failed (failure %d)", url.c_str(), static_cast<int>(http_client_get_failure(client)));
        return nullptr;
    }
    const int statusCode = http_client_get_status_code(client);
    if (statusCode != 200) {
        LOG_E(TAG, "Request for %s failed with status %d", url.c_str(), statusCode);
        http_client_close(client);
        return nullptr;
    }
    return client;
}

void copyJsonString(const cJSON* json, const char* key, char* target, size_t targetSize) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(json, key);
    target[0] = '\0';
    if (cJSON_IsString(item)) {
        strncpy(target, item->valuestring, targetSize - 1);
        target[targetSize - 1] = '\0';
    }
}

/** @param[out] layoutId the partition layout that the release was built for, empty when the manifest doesn't say */
bool parseManifest(const char* text, Release& release, char* file, size_t fileSize, char* layoutId, size_t layoutIdSize) {
    cJSON* json = cJSON_Parse(text);
    if (json == nullptr) {
        return false;
    }
    release = {};
    copyJsonString(json, "version", release.version, sizeof(release.version));
    copyJsonString(json, "gitCommit", release.gitCommit, sizeof(release.gitCommit));
    copyJsonString(json, "layoutId", layoutId, layoutIdSize);
    const cJSON* created = cJSON_GetObjectItemCaseSensitive(json, "created");
    const cJSON* size = cJSON_GetObjectItemCaseSensitive(json, "size");
    const cJSON* fileItem = cJSON_GetObjectItemCaseSensitive(json, "file");
    const bool valid = cJSON_IsNumber(created) && cJSON_IsNumber(size) && size->valuedouble > 0 && cJSON_IsString(fileItem) &&
        strlen(fileItem->valuestring) < fileSize && strchr(fileItem->valuestring, '/') == nullptr && release.gitCommit[0] != '\0';
    if (valid) {
        release.created = static_cast<int64_t>(created->valuedouble);
        release.size = static_cast<uint32_t>(size->valuedouble);
        strcpy(file, fileItem->valuestring);
    }
    cJSON_Delete(json);
    return valid;
}

// endregion

// region Check

bool isCheckDue(bool force) {
    // Without a synced clock the time of the last check can't be compared
    if (force || !network::ntp::isSynced()) {
        return true;
    }
    const int64_t lastCheck = loadLastCheck();
    const int64_t now = time(nullptr);
    return lastCheck <= 0 || now < lastCheck || now - lastCheck >= CHECK_INTERVAL_SECONDS;
}

/** Within the channel of the build only a newer build is an update. The build of another channel is one when it differs. */
bool isUpdate(const Release& release, Channel channel) {
    if (strcmp(release.gitCommit, tt_build_git_commit) == 0) {
        return false;
    }
    return channel != getBuildChannel() || release.created > tt_build_timestamp;
}

/**
 * An OTA update only writes the app partition, so a release for another partition table, or one that doesn't fit the
 * partition of this device, is installed with the web flasher instead
 */
bool requiresFullInstall(const Release& release, const char* layoutId) {
    const FirmwareOps* ops = nullptr;
    void* ctx = nullptr;
    if (firmware_get(FIRMWARE_ID_SYSTEM, &ops, &ctx) != ERROR_NONE) {
        return false;
    }
    FirmwareInfo info {};
    if (ops->get_info(ctx, &info) != ERROR_NONE) {
        return false;
    }
    if (info.max_image_size > 0 && release.size > info.max_image_size) {
        LOG_W(TAG, "The update of %lu bytes doesn't fit the partition of %lu bytes", static_cast<unsigned long>(release.size), static_cast<unsigned long>(info.max_image_size));
        return true;
    }
    if (layoutId[0] != '\0' && info.layout_id[0] != '\0' && strcmp(layoutId, info.layout_id) != 0) {
        LOG_W(TAG, "The update is built for partition table %s, this device has %s", layoutId, info.layout_id);
        return true;
    }
    return false;
}

void runCheck(bool force) {
    lock();
    const Status status = state->snapshot.status;
    const Channel channel = state->snapshot.channel;
    const uint32_t generation = state->channelGeneration;
    const bool forceUpdate = state->forceUpdateRequested;
    state->forceUpdateRequested = false;
    unlock();
    // A restart is needed first
    if (status == Status::Installed || !isCheckDue(force)) {
        return;
    }

    setStatus(Status::Checking);
    const ::std::string url = getUrl(channel, CONFIG_TT_DEVICE_ID "-ota.json");
    HttpClient* client = openUrl(url);
    char* text = static_cast<char*>(malloc(MANIFEST_MAX_SIZE + 1));
    size_t length = 0;
    bool received = client != nullptr && text != nullptr;
    while (received) {
        const int read = http_client_read(client, text + length, MANIFEST_MAX_SIZE - length);
        if (read < 0) {
            received = false;
        } else if (read == 0) {
            break;
        } else {
            length += read;
            if (length == MANIFEST_MAX_SIZE) {
                received = false;
            }
        }
    }
    if (client != nullptr) {
        http_client_close(client);
    }

    Release release {};
    char file[sizeof(State::releaseFile)] = "";
    char layoutId[sizeof(FirmwareInfo::layout_id)] = "";
    bool parsed = false;
    if (received) {
        text[length] = '\0';
        parsed = parseManifest(text, release, file, sizeof(file), layoutId, sizeof(layoutId));
        if (!parsed) {
            LOG_E(TAG, "Invalid manifest at %s", url.c_str());
        }
    }
    free(text);

    if (!parsed) {
        lock();
        if (state->channelGeneration == generation) {
            state->snapshot.status = Status::CheckFailed;
            notifySubscribers();
        }
        unlock();
        return;
    }

    if (network::ntp::isSynced()) {
        const int64_t now = time(nullptr);
        writeSettings([now](Preferences* preferences) {
            preferences_put_int64(preferences, SETTING_LAST_CHECK, now);
        });
    }

    const bool available = forceUpdate || isUpdate(release, channel);
    const bool fullInstallRequired = requiresFullInstall(release, layoutId);
    LOG_I(TAG, "Found %s %s: %s", release.version, release.gitCommit, available ? "update available" : "up to date");
    lock();
    // The channel changed during the check, and a check of the new channel follows
    const bool current = state->channelGeneration == generation;
    if (current) {
        // The failure stays visible when the check after a failed install finds the same release
        const bool failedBefore = status == Status::InstallFailed && strcmp(state->snapshot.release.gitCommit, release.gitCommit) == 0;
        strcpy(state->releaseFile, file);
        state->snapshot.release = release;
        state->snapshot.hasRelease = true;
        state->snapshot.status = !available ? Status::UpToDate : (failedBefore ? Status::InstallFailed : Status::Available);
        state->snapshot.progress = 0;
        state->snapshot.fullInstallRequired = fullInstallRequired;
        if (forceUpdate && !fullInstallRequired) {
            // The user confirmed the forced update before the release was known
            strcpy(state->installCommit, release.gitCommit);
            state->installRequested = true;
        }
        notifySubscribers();
    }
    unlock();
    if (!current) {
        return;
    }
    if (available) {
        showNotification(release);
    } else {
        dismissNotification();
    }
}

// endregion

// region Install

/** @return the error, and ERROR_NONE when the update is installed and activated */
error_t writeUpdate(HttpClient* client, const FirmwareOps* ops, void* ctx, uint32_t size) {
    FirmwareUpdateRequest request { .image_size = size, .flags = 0 };
    FirmwareUpdateHandle* handle = nullptr;
    error_t error = ops->begin(ctx, &request, &handle);
    if (error != ERROR_NONE) {
        return error;
    }
    auto* buffer = static_cast<uint8_t*>(malloc(DOWNLOAD_CHUNK_SIZE));
    if (buffer == nullptr) {
        ops->abort(handle);
        return ERROR_OUT_OF_MEMORY;
    }
    setStatus(Status::Installing, 0, true);
    uint32_t written = 0;
    uint8_t lastProgress = 0;
    while (written < size) {
        const int read = http_client_read(client, buffer, DOWNLOAD_CHUNK_SIZE);
        if (read <= 0) {
            error = ERROR_RESOURCE;
            break;
        }
        if (written + read > size) {
            error = ERROR_OUT_OF_RANGE;
            break;
        }
        error = ops->write(handle, buffer, read);
        if (error != ERROR_NONE) {
            break;
        }
        written += read;
        const auto progress = static_cast<uint8_t>(static_cast<uint64_t>(written) * 100 / size);
        if (progress != lastProgress) {
            lastProgress = progress;
            setStatus(Status::Installing, progress, true);
        }
    }
    free(buffer);
    if (error != ERROR_NONE) {
        ops->abort(handle);
        return error;
    }
    error = ops->finish(handle);
    if (error != ERROR_NONE) {
        return error;
    }
    return ops->activate(ctx);
}

void runInstall() {
    // Reserved in one step, so a channel change either discards the release first or is rejected while installing
    lock();
    const Status status = state->snapshot.status;
    const bool installable = (status == Status::Available || status == Status::InstallFailed) && !state->snapshot.fullInstallRequired &&
        strcmp(state->snapshot.release.gitCommit, state->installCommit) == 0;
    const Release release = state->snapshot.release;
    const Channel channel = state->snapshot.channel;
    char file[sizeof(State::releaseFile)];
    strcpy(file, state->releaseFile);
    if (installable) {
        state->snapshot.status = Status::Installing;
        state->snapshot.progress = 0;
        state->snapshot.downloading = false;
        notifySubscribers();
    }
    unlock();
    if (!installable) {
        return;
    }

    const FirmwareOps* ops = nullptr;
    void* ctx = nullptr;
    if (firmware_get(FIRMWARE_ID_SYSTEM, &ops, &ctx) != ERROR_NONE) {
        setStatus(Status::InstallFailed);
        return;
    }

    LOG_I(TAG, "Installing %s", file);
    error_t error = ERROR_RESOURCE;
    HttpClient* client = openUrl(getUrl(channel, file));
    if (client != nullptr) {
        const int64_t length = http_client_get_content_length(client);
        if (length >= 0 && length != release.size) {
            LOG_E(TAG, "The download has %lld bytes instead of %lu", static_cast<long long>(length), static_cast<unsigned long>(release.size));
            error = ERROR_INVALID_STATE;
        } else {
            error = writeUpdate(client, ops, ctx, release.size);
        }
        http_client_close(client);
    }

    if (error != ERROR_NONE) {
        LOG_E(TAG, "Install failed: %s", error_to_string(error));
        setStatus(Status::InstallFailed);
        // The image can be of a build that was published after the last check, so a retry needs the current manifest
        requestCheck(true);
        return;
    }
    LOG_I(TAG, "Installed, the update runs after a restart");
    setStatus(Status::Installed);
    dismissNotification();
}

// endregion

// region Worker

int32_t workerMain(void* /*context*/) {
    while (true) {
        lock();
        if (state->stopping || (!state->checkRequested && !state->installRequested)) {
            state->workerRunning = false;
            unlock();
            return 0;
        }
        // A forced update checks first, then installs what the check found
        const bool install = state->installRequested && !state->forceUpdateRequested;
        const bool force = state->checkForced;
        if (install) {
            state->installRequested = false;
        } else {
            state->checkRequested = false;
            state->checkForced = false;
        }
        unlock();

        if (install) {
            runInstall();
        } else {
            runCheck(force);
        }
    }
}

/** Requires the global lock */
error_t ensureWorkerRunning() {
    if (state->stopping) {
        return ERROR_INVALID_STATE;
    }
    if (state->workerRunning) {
        return ERROR_NONE;
    }
    if (state->worker != nullptr) {
        // The previous worker cleared workerRunning and exits without taking the lock
        thread_join(state->worker, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(state->worker);
    }
    state->worker = thread_alloc_full("fwupdate", WORKER_STACK_SIZE, workerMain, nullptr, -1);
    if (state->worker == nullptr) {
        LOG_E(TAG, "Failed to create the worker");
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

void onNetworkConnected(SystemEvent* /*event*/, void* /*context*/) {
    requestCheck(false);
}

// endregion

// region Service

void* createService(const ::ServiceManifest* /*manifest*/) {
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

void destroyService(const ::ServiceManifest* /*manifest*/, void* data) {
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

error_t onStart(::ServiceInstance* /*instance*/, void* data) {
    if (data == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    const Channel channel = loadChannel();
    const FirmwareOps* ops = nullptr;
    void* ctx = nullptr;
    const bool installSupported = firmware_get(FIRMWARE_ID_SYSTEM, &ops, &ctx) == ERROR_NONE;
    lock();
    state->stopping = false;
    state->snapshot.channel = channel;
    state->snapshot.installSupported = installSupported;
    unlock();
    return system_event_callback_add(KERNEL_EVENT_NETWORK_CONNECTED, onNetworkConnected, nullptr);
}

void onStop(::ServiceInstance* /*instance*/, void* /*data*/) {
    system_event_callback_remove(KERNEL_EVENT_NETWORK_CONNECTED, onNetworkConnected);
    lock();
    state->stopping = true;
    if (state->client != nullptr) {
        http_client_cancel(state->client);
    }
    Thread* worker = state->worker;
    state->worker = nullptr;
    unlock();
    if (worker != nullptr) {
        thread_join(worker, portMAX_DELAY, pdMS_TO_TICKS(10));
        thread_free(worker);
    }
    lock();
    http_client_free(state->client);
    state->client = nullptr;
    unlock();
}

// endregion

} // namespace

void requestCheck(bool force) {
    if (!lockState()) {
        return;
    }
    state->checkRequested = true;
    state->checkForced |= force;
    ensureWorkerRunning();
    unlock();
}

error_t requestInstall(const char* gitCommit) {
    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    error_t error = ERROR_NONE;
    const Status status = state->snapshot.status;
    if (!state->snapshot.installSupported || state->snapshot.fullInstallRequired) {
        error = ERROR_NOT_SUPPORTED;
    } else if ((status != Status::Available && status != Status::InstallFailed) || strcmp(state->snapshot.release.gitCommit, gitCommit) != 0) {
        error = ERROR_INVALID_STATE;
    } else {
        strcpy(state->installCommit, state->snapshot.release.gitCommit);
        state->installRequested = true;
        error = ensureWorkerRunning();
        if (error != ERROR_NONE) {
            state->installRequested = false;
        }
    }
    unlock();
    return error;
}

error_t requestForcedUpdate() {
    if (!lockState()) {
        return ERROR_INVALID_STATE;
    }
    error_t error = ERROR_NONE;
    const Status status = state->snapshot.status;
    if (!state->snapshot.installSupported) {
        error = ERROR_NOT_SUPPORTED;
    } else if (status == Status::Installing || status == Status::Installed) {
        error = ERROR_INVALID_STATE;
    } else {
        state->forceUpdateRequested = true;
        state->checkRequested = true;
        state->checkForced = true;
        error = ensureWorkerRunning();
        if (error != ERROR_NONE) {
            state->forceUpdateRequested = false;
        }
    }
    unlock();
    return error;
}

Snapshot getSnapshot() {
    Snapshot snapshot {};
    if (lockState()) {
        snapshot = state->snapshot;
        unlock();
    }
    return snapshot;
}

void setChannel(Channel channel) {
    if (!lockState()) {
        return;
    }
    const bool changed = state->snapshot.channel != channel && state->snapshot.status != Status::Installing;
    if (changed) {
        state->channelGeneration++;
        state->snapshot.channel = channel;
        state->snapshot.hasRelease = false;
        state->snapshot.status = Status::Unknown;
        notifySubscribers();
    }
    unlock();
    if (changed) {
        writeSettings([channel](Preferences* preferences) {
            preferences_put_string(preferences, SETTING_CHANNEL, getChannelSetting(channel));
        });
        dismissNotification();
        requestCheck(true);
    }
}

error_t subscribe(TaskEventGroup* eventGroup, uint32_t* outBit) {
    uint32_t bit = 0;
    const error_t claimed = task_event_group_claim_bit(eventGroup, &bit);
    if (claimed != ERROR_NONE) {
        return claimed;
    }
    if (!lockState()) {
        task_event_group_release_bit(eventGroup, bit);
        return ERROR_INVALID_STATE;
    }
    error_t error = ERROR_OUT_OF_RANGE;
    for (auto& subscription : state->subscriptions) {
        if (subscription.eventGroup == nullptr) {
            subscription = { eventGroup, bit };
            error = ERROR_NONE;
            break;
        }
    }
    unlock();
    if (error != ERROR_NONE) {
        task_event_group_release_bit(eventGroup, bit);
        return error;
    }
    *outBit = bit;
    return ERROR_NONE;
}

void unsubscribe(TaskEventGroup* eventGroup, uint32_t bit) {
    if (lockState()) {
        for (auto& subscription : state->subscriptions) {
            if (subscription.eventGroup == eventGroup && subscription.bit == bit) {
                subscription = {};
            }
        }
        unlock();
    }
    task_event_group_release_bit(eventGroup, bit);
}

extern const ::ServiceManifest manifest = {
    .id = SERVICE_ID,
    .create_service = createService,
    .destroy_service = destroyService,
    .on_start = onStart,
    .on_stop = onStop
};

} // namespace tt::service::firmwareupdate
