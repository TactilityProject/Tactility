// SPDX-License-Identifier: Apache-2.0
#include <notification/notification.h>
#include <notification/notification_state.h>

#include <service/paths.h>

#include <tactility/Memory.hpp>
#include <tactility/paths.h>
#include <tactility/file_ops.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/properties_file.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/delay.h>
#include <tactility/freertos/task.h>
#include <tactility/time.h>

#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

constexpr auto* TAG = "notification";
constexpr int64_t MIN_VALID_WALL_TIME = 1700000000;
/** The sources are stored like a service's settings */
constexpr auto* SETTINGS_ID = "tactility.notification";
constexpr auto* SETTINGS_FILE = "sources.properties";
constexpr auto* KEY_APP_PREFIX = "app.";
constexpr auto* KEY_SERVICE_PREFIX = "service.";

/** An action that runs without the lock, which the notification can't be removed during, as its sender may free what it uses */
struct ActionRun {
    uint32_t id;
    TaskHandle_t task;
};

constexpr size_t MAX_ACTION_RUNS = 8;

struct State {
    ActionRun runs[MAX_ACTION_RUNS] = {};
    size_t runCount = 0;
    NotificationSource sources[NOTIFICATION_SOURCE_LIMIT] = {};
    size_t sourceCount = 0;
    /** Whether the sources were read from storage */
    bool sourcesLoaded = false;
    Notification entries[NOTIFICATION_HISTORY_CAPACITY] = {};
    /** Oldest first */
    size_t count = 0;
    uint32_t nextId = 1;
    NotificationSubscription* subscriptions = nullptr;
    /** Counts the events, which says in what order things happened without depending on the clock */
    uint32_t eventOrder = 0;
};

struct Lock {
    Mutex handle {};
    Lock() { mutex_construct(&handle); }
    ~Lock() { mutex_destruct(&handle); }
};

Lock& lockInstance() {
    static Lock instance;
    return instance;
}

/** Sources as they were stored, read without the lock */
struct StoredSource {
    NotificationSourceType type;
    char id[NOTIFICATION_SOURCE_ID_MAX_LENGTH + 1];
    bool enabled;
};

struct StoredSources {
    StoredSource sources[NOTIFICATION_SOURCE_LIMIT];
    size_t count = 0;
};

static void onStoredEntry(const char* key, const char* value, void* context) {
    auto* stored = static_cast<StoredSources*>(context);
    if (stored->count >= NOTIFICATION_SOURCE_LIMIT) {
        return;
    }
    StoredSource& source = stored->sources[stored->count];
    const size_t appLength = strlen(KEY_APP_PREFIX);
    const size_t serviceLength = strlen(KEY_SERVICE_PREFIX);
    const char* id;
    if (strncmp(key, KEY_APP_PREFIX, appLength) == 0) {
        source.type = NOTIFICATION_SOURCE_APP;
        id = key + appLength;
    } else if (strncmp(key, KEY_SERVICE_PREFIX, serviceLength) == 0) {
        source.type = NOTIFICATION_SOURCE_SERVICE;
        id = key + serviceLength;
    } else {
        return;
    }
    if (id[0] == '\0' || strlen(id) > NOTIFICATION_SOURCE_ID_MAX_LENGTH) {
        return;
    }
    strncpy(source.id, id, sizeof(source.id) - 1);
    source.id[sizeof(source.id) - 1] = '\0';
    source.enabled = strcmp(value, "false") != 0;
    stored->count++;
}

/** Work for the file operations task */
struct SaveJob {
    StoredSources sources;
};

static void saveSources(void* context) {
    auto* job = static_cast<SaveJob*>(context);
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(SETTINGS_ID, directory, sizeof(directory)) == ERROR_NONE &&
        service_paths_get_user_data_path(SETTINGS_ID, SETTINGS_FILE, path, sizeof(path)) == ERROR_NONE) {
        const error_t made = directory_make(directory, true);
        PropertiesFile* file = (made == ERROR_NONE || made == ERROR_ALREADY_EXISTS) ? properties_file_open(path) : nullptr;
        if (file != nullptr) {
            for (size_t i = 0; i < job->sources.count; i++) {
                const StoredSource& source = job->sources.sources[i];
                char key[NOTIFICATION_SOURCE_ID_MAX_LENGTH + 16];
                snprintf(key, sizeof(key), "%s%s", source.type == NOTIFICATION_SOURCE_APP ? KEY_APP_PREFIX : KEY_SERVICE_PREFIX, source.id);
                properties_file_set(file, key, source.enabled ? "true" : "false");
            }
            if (properties_file_close(file) != ERROR_NONE) {
                LOG_W(TAG, "Failed to save the sources");
            }
        }
    }
    tt::deleteObject(job);
}

/** The state is only allocated while the module is started. Guarded by the lock. */
State* state = nullptr;

void lock() { mutex_lock(&lockInstance().handle); }
void unlock() { mutex_unlock(&lockInstance().handle); }

/** Requires the lock. @param[in] id the notification, or 0 for any. Actions on the current task don't count, as they can't wait for themselves. */
bool hasRunningAction(uint32_t id) {
    const TaskHandle_t current = xTaskGetCurrentTaskHandle();
    for (size_t i = 0; i < state->runCount; i++) {
        if ((id == 0 || state->runs[i].id == id) && state->runs[i].task != current) {
            return true;
        }
    }
    return false;
}

/**
 * Requires the lock, which is released while waiting. Returns when no action of the notification (or of any when @a id is 0)
 * runs on another task. The notifications can change meanwhile, so what was read before the call must be read again.
 * @return false when the module stopped meanwhile
 */
bool waitForActions(uint32_t id) {
    while (state != nullptr && hasRunningAction(id)) {
        unlock();
        delay_millis(1);
        lock();
    }
    return state != nullptr;
}

/** Requires the lock. Writes the sources on the file operations task. */
void queueSave() {
    auto* job = tt::newObject<SaveJob>();
    if (job == nullptr) {
        return;
    }
    for (size_t i = 0; i < state->sourceCount; i++) {
        StoredSource& stored = job->sources.sources[i];
        stored.type = state->sources[i].type;
        memcpy(stored.id, state->sources[i].id, sizeof(stored.id));
        stored.enabled = state->sources[i].enabled;
    }
    job->sources.count = state->sourceCount;
    if (file_ops_queue(saveSources, job, nullptr) != ERROR_NONE) {
        tt::deleteObject(job);
    }
}

/** Requires the lock */
NotificationSource* findSource(NotificationSourceType type, const char* id) {
    for (size_t i = 0; i < state->sourceCount; i++) {
        if (state->sources[i].type == type && strcmp(state->sources[i].id, id) == 0) {
            return &state->sources[i];
        }
    }
    return nullptr;
}

/** Reads the stored sources once. The file is read without the lock, as other threads show the notifications. */
void ensureSourcesLoaded() {
    lock();
    const bool loaded = state == nullptr || state->sourcesLoaded;
    unlock();
    if (loaded) {
        return;
    }

    auto* stored = tt::newObject<StoredSources>();
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (stored != nullptr && service_paths_get_user_data_path(SETTINGS_ID, SETTINGS_FILE, path, sizeof(path)) == ERROR_NONE) {
        if (PropertiesFile* file = properties_file_open(path)) {
            properties_file_for_each(file, onStoredEntry, stored);
            // Only read, so nothing is written back over what a sender saved meanwhile
            properties_file_discard(file);
        }
    }

    lock();
    if (state != nullptr && !state->sourcesLoaded) {
        state->sourcesLoaded = true;
        if (stored != nullptr) {
            for (size_t i = 0; i < stored->count; i++) {
                // A source that registered while the file was read keeps what the sender did
                if (state->sourceCount < NOTIFICATION_SOURCE_LIMIT && findSource(stored->sources[i].type, stored->sources[i].id) == nullptr) {
                    NotificationSource& source = state->sources[state->sourceCount++];
                    source.type = stored->sources[i].type;
                    memcpy(source.id, stored->sources[i].id, sizeof(source.id));
                    source.enabled = stored->sources[i].enabled;
                }
            }
        }
    }
    unlock();
    if (stored != nullptr) {
        tt::deleteObject(stored);
    }
}

/** Copies text and cuts it at a UTF-8 character boundary when it doesn't fit */
void copyText(char* destination, size_t capacity, const char* source) {
    if (source == nullptr) {
        source = "";
    }
    size_t length = strnlen(source, capacity - 1);
    if (length == capacity - 1) {
        while (length > 0 && (static_cast<uint8_t>(source[length]) & 0xC0) == 0x80) {
            length--;
        }
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

NotificationContext makeContext(NotificationSourceType type, const char* sourceId, const char* message) {
    NotificationContext context {};
    context.source_type = type;
    copyText(context.source_id, sizeof(context.source_id), sourceId);
    copyText(context.message, sizeof(context.message), message);
    return context;
}

/** Requires the lock */
void emitEvent(NotificationEventType type, uint32_t id, bool bumped = false) {
    const NotificationEvent event = { .timestamp = get_millis(), .type = type, .id = id, .bumped = bumped, .order = ++state->eventOrder };
    for (auto* sub = state->subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.count >= NOTIFICATION_EVENT_QUEUE_CAPACITY) {
            continue;
        }
        const uint8_t tail = (sub->internal.head + sub->internal.count) % NOTIFICATION_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = event;
        sub->internal.count++;
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
}

/** Requires the lock. Frees the data of the entry and moves the newer ones down. */
void removeEntry(size_t index) {
    bundle_free(state->entries[index].data);
    for (size_t i = index + 1; i < state->count; i++) {
        state->entries[i - 1] = state->entries[i];
    }
    state->count--;
}

/** Requires the lock. @return the copy of the entry for the reader */
error_t copyEntry(const Notification& entry, Notification* out) {
    *out = entry;
    if (entry.data != nullptr) {
        out->data = bundle_clone(entry.data);
        if (out->data == nullptr) {
            return ERROR_OUT_OF_MEMORY;
        }
    }
    return ERROR_NONE;
}

} // namespace

error_t notification_state_start(void) {
    auto* created = tt::newObject<State>();
    if (created == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    lock();
    state = created;
    unlock();
    return ERROR_NONE;
}

void notification_state_stop(void) {
    lock();
    State* stopped = state;
    state = nullptr;
    unlock();
    if (stopped == nullptr) {
        return;
    }
    for (size_t i = 0; i < stopped->count; i++) {
        bundle_free(stopped->entries[i].data);
    }
    tt::deleteObject(stopped);
}

extern "C" {

void notification_release(Notification* notification) {
    bundle_free(notification->data);
    notification->data = nullptr;
}

NotificationContext notification_context_for_app(const char* app_id, const char* message) {
    return makeContext(NOTIFICATION_SOURCE_APP, app_id, message);
}

NotificationContext notification_context_for_service(const char* service_id, const char* message) {
    return makeContext(NOTIFICATION_SOURCE_SERVICE, service_id, message);
}

void notification_context_set_title(NotificationContext* context, const char* title) {
    copyText(context->title, sizeof(context->title), title);
}

void notification_context_set_severity(NotificationContext* context, NotificationSeverity severity) {
    context->severity = severity;
}

Bundle* notification_context_get_data(NotificationContext* context) {
    if (context->data == nullptr) {
        context->data = bundle_alloc();
    }
    return context->data;
}

/** Requires the lock. Takes the Bundle of the context. */
static void fillEntry(Notification& entry, uint32_t id, NotificationContext* context) {
    entry = {};
    entry.id = id;
    entry.source_type = context->source_type;
    memcpy(entry.source_id, context->source_id, sizeof(entry.source_id));
    memcpy(entry.title, context->title, sizeof(entry.title));
    memcpy(entry.message, context->message, sizeof(entry.message));
    entry.severity = context->severity;
    const int64_t wallTime = static_cast<int64_t>(time(nullptr));
    entry.wall_time = wallTime >= MIN_VALID_WALL_TIME ? wallTime : 0;
    entry.uptime_ms = get_millis();
    entry.data = context->data;
    context->data = nullptr;
    entry.action = context->action;
    entry.action_data = context->action_data;
    entry.sticky = context->sticky;
}

error_t notification_context_for_notification(uint32_t id, NotificationContext* context) {
    lock();
    if (state == nullptr) {
        unlock();
        return ERROR_INVALID_STATE;
    }
    for (size_t i = 0; i < state->count; i++) {
        const Notification& entry = state->entries[i];
        if (entry.id != id) {
            continue;
        }
        Bundle* data = nullptr;
        if (entry.data != nullptr) {
            data = bundle_clone(entry.data);
            if (data == nullptr) {
                unlock();
                return ERROR_OUT_OF_MEMORY;
            }
        }
        *context = {};
        context->source_type = entry.source_type;
        memcpy(context->source_id, entry.source_id, sizeof(context->source_id));
        memcpy(context->title, entry.title, sizeof(context->title));
        memcpy(context->message, entry.message, sizeof(context->message));
        context->severity = entry.severity;
        context->data = data;
        context->action = entry.action;
        context->action_data = entry.action_data;
        context->sticky = entry.sticky;
        unlock();
        return ERROR_NONE;
    }
    unlock();
    return ERROR_NOT_FOUND;
}

error_t notification_update(uint32_t id, NotificationContext* context, bool bump) {
    if (context->message[0] == '\0' || context->source_id[0] == '\0') {
        return ERROR_INVALID_ARGUMENT;
    }
    lock();
    if (state == nullptr || !waitForActions(id)) {
        unlock();
        return ERROR_INVALID_STATE;
    }
    for (size_t i = 0; i < state->count; i++) {
        if (state->entries[i].id != id) {
            continue;
        }
        bundle_free(state->entries[i].data);
        size_t index = i;
        const int64_t wallTime = state->entries[i].wall_time;
        const uint64_t uptime = state->entries[i].uptime_ms;
        if (bump) {
            // The newest notification is the last one
            for (size_t j = i + 1; j < state->count; j++) {
                state->entries[j - 1] = state->entries[j];
            }
            index = state->count - 1;
        }
        fillEntry(state->entries[index], id, context);
        if (!bump) {
            state->entries[index].wall_time = wallTime;
            state->entries[index].uptime_ms = uptime;
        }
        emitEvent(NOTIFICATION_EVENT_UPDATED, id, bump);
        unlock();
        return ERROR_NONE;
    }
    unlock();
    return ERROR_NOT_FOUND;
}

error_t notification_send(NotificationContext* context, uint32_t* out_id) {
    if (context->message[0] == '\0' || context->source_id[0] == '\0') {
        return ERROR_INVALID_ARGUMENT;
    }
    ensureSourcesLoaded();
    lock();
    if (state == nullptr) {
        unlock();
        return ERROR_INVALID_STATE;
    }
    NotificationSource* source = findSource(context->source_type, context->source_id);
    if (source == nullptr && state->sourceCount < NOTIFICATION_SOURCE_LIMIT) {
        // The user can disable it from now on
        source = &state->sources[state->sourceCount++];
        source->type = context->source_type;
        memcpy(source->id, context->source_id, sizeof(source->id));
        source->enabled = true;
        queueSave();
    }
    if (source != nullptr && !source->enabled) {
        unlock();
        return ERROR_NOT_ALLOWED;
    }
    while (state->count == NOTIFICATION_HISTORY_CAPACITY) {
        const uint32_t evictedId = state->entries[0].id;
        // An action of the oldest notification can still be running, so it can't go yet
        if (!waitForActions(evictedId)) {
            unlock();
            return ERROR_INVALID_STATE;
        }
        if (state->count == NOTIFICATION_HISTORY_CAPACITY && state->entries[0].id == evictedId) {
            removeEntry(0);
            emitEvent(NOTIFICATION_EVENT_DISMISSED, evictedId);
        }
    }

    Notification& entry = state->entries[state->count++];
    fillEntry(entry, state->nextId++, context);
    if (out_id != nullptr) {
        *out_id = entry.id;
    }
    emitEvent(NOTIFICATION_EVENT_POSTED, entry.id);
    unlock();
    return ERROR_NONE;
}

void notification_context_set_action(NotificationContext* context, NotificationAction action, void* user_data) {
    context->action = action;
    context->action_data = user_data;
}

void notification_context_set_sticky(NotificationContext* context, bool sticky) {
    context->sticky = sticky;
}

void notification_context_destroy(NotificationContext* context) {
    bundle_free(context->data);
    context->data = nullptr;
}

error_t notification_get(uint32_t id, Notification* notification) {
    lock();
    error_t result = ERROR_NOT_FOUND;
    if (state != nullptr) {
        for (size_t i = 0; i < state->count; i++) {
            if (state->entries[i].id == id) {
                result = copyEntry(state->entries[i], notification);
                break;
            }
        }
    }
    unlock();
    return result;
}

error_t notification_get_all(Notification* notifications, size_t max_count, size_t* count) {
    *count = 0;
    lock();
    error_t result = ERROR_NONE;
    if (state != nullptr) {
        for (size_t i = state->count; i > 0 && *count < max_count; i--) {
            result = copyEntry(state->entries[i - 1], &notifications[*count]);
            if (result != ERROR_NONE) {
                break;
            }
            (*count)++;
        }
    }
    unlock();
    if (result != ERROR_NONE) {
        for (size_t i = 0; i < *count; i++) {
            notification_release(&notifications[i]);
        }
        *count = 0;
    }
    return result;
}

void notification_preload(void) {
    ensureSourcesLoaded();
}

error_t notification_get_sources(NotificationSource* sources, size_t max_count, size_t* count) {
    *count = 0;
    ensureSourcesLoaded();
    lock();
    if (state != nullptr) {
        for (size_t i = 0; i < state->sourceCount && *count < max_count; i++) {
            sources[(*count)++] = state->sources[i];
        }
    }
    unlock();
    return ERROR_NONE;
}

error_t notification_set_source_enabled(NotificationSourceType type, const char* id, bool enabled) {
    ensureSourcesLoaded();
    lock();
    NotificationSource* source = state != nullptr ? findSource(type, id) : nullptr;
    if (source == nullptr) {
        unlock();
        return ERROR_NOT_FOUND;
    }
    if (source->enabled == enabled) {
        unlock();
        return ERROR_NONE;
    }
    source->enabled = enabled;
    if (!enabled && waitForActions(0)) {
        // The pending notifications of the sender go with it
        for (size_t i = state->count; i > 0; i--) {
            const Notification& entry = state->entries[i - 1];
            if (entry.source_type == type && strcmp(entry.source_id, id) == 0) {
                const uint32_t removedId = entry.id;
                removeEntry(i - 1);
                emitEvent(NOTIFICATION_EVENT_DISMISSED, removedId);
            }
        }
    }
    queueSave();
    unlock();
    return ERROR_NONE;
}

uint32_t notification_get_event_order(void) {
    lock();
    const uint32_t order = state != nullptr ? state->eventOrder : 0;
    unlock();
    return order;
}

size_t notification_get_count(void) {
    lock();
    const size_t count = state != nullptr ? state->count : 0;
    unlock();
    return count;
}

error_t notification_dismiss(uint32_t id) {
    lock();
    error_t result = ERROR_NOT_FOUND;
    if (state != nullptr && waitForActions(id)) {
        for (size_t i = 0; i < state->count; i++) {
            if (state->entries[i].id == id) {
                removeEntry(i);
                emitEvent(NOTIFICATION_EVENT_DISMISSED, id);
                result = ERROR_NONE;
                break;
            }
        }
    }
    unlock();
    return result;
}

error_t notification_invoke_action(uint32_t id) {
    NotificationAction action = nullptr;
    void* actionData = nullptr;
    error_t result = ERROR_NOT_FOUND;
    lock();
    if (state != nullptr) {
        for (size_t i = 0; i < state->count; i++) {
            if (state->entries[i].id == id) {
                action = state->entries[i].action;
                actionData = state->entries[i].action_data;
                result = action != nullptr ? ERROR_NONE : ERROR_NOT_SUPPORTED;
                break;
            }
        }
    }
    // Registered before the lock is released, so removing the notification waits for the action to return
    const bool registered = action != nullptr && state->runCount < MAX_ACTION_RUNS;
    if (registered) {
        state->runs[state->runCount++] = { id, xTaskGetCurrentTaskHandle() };
    }
    unlock();
    // Called without the lock, so the action can use the module
    if (action != nullptr) {
        action(id, actionData);
    }
    if (registered) {
        lock();
        if (state != nullptr) {
            for (size_t i = 0; i < state->runCount; i++) {
                if (state->runs[i].id == id && state->runs[i].task == xTaskGetCurrentTaskHandle()) {
                    state->runs[i] = state->runs[--state->runCount];
                    break;
                }
            }
        }
        unlock();
    }
    return result;
}

error_t notification_dismiss_unless_sticky(uint32_t id) {
    lock();
    error_t result = ERROR_NOT_FOUND;
    if (state != nullptr && waitForActions(id)) {
        for (size_t i = 0; i < state->count; i++) {
            if (state->entries[i].id == id) {
                // The flag is read under the lock, so a sender that made it sticky meanwhile is respected
                if (state->entries[i].sticky) {
                    result = ERROR_NOT_ALLOWED;
                } else {
                    removeEntry(i);
                    emitEvent(NOTIFICATION_EVENT_DISMISSED, id);
                    result = ERROR_NONE;
                }
                break;
            }
        }
    }
    unlock();
    return result;
}

void notification_dismiss_non_sticky(void) {
    lock();
    if (state != nullptr && waitForActions(0)) {
        size_t i = 0;
        while (i < state->count) {
            if (state->entries[i].sticky) {
                i++;
            } else {
                const uint32_t removedId = state->entries[i].id;
                removeEntry(i);
                emitEvent(NOTIFICATION_EVENT_DISMISSED, removedId);
            }
        }
    }
    unlock();
}

void notification_dismiss_all(void) {
    lock();
    if (state != nullptr && waitForActions(0) && state->count > 0) {
        for (size_t i = 0; i < state->count; i++) {
            bundle_free(state->entries[i].data);
        }
        state->count = 0;
        emitEvent(NOTIFICATION_EVENT_CLEARED, 0);
    }
    unlock();
}

error_t notification_subscribe(NotificationSubscription* sub, TaskEventGroup* event_group) {
    uint32_t bit;
    const error_t claimed = task_event_group_claim_bit(event_group, &bit);
    if (claimed != ERROR_NONE) {
        return claimed;
    }
    lock();
    if (state == nullptr) {
        unlock();
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

error_t notification_unsubscribe(NotificationSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;
    lock();
    if (state != nullptr) {
        for (auto** link = &state->subscriptions; *link != nullptr; link = &(*link)->internal.next) {
            if (*link == sub) {
                *link = sub->internal.next;
                result = ERROR_NONE;
                break;
            }
        }
    }
    unlock();
    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }
    return result;
}

error_t notification_poll(NotificationSubscription* sub, NotificationEvent* event) {
    lock();
    const bool hasEvent = sub->internal.count > 0;
    if (hasEvent) {
        *event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % NOTIFICATION_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    unlock();
    return hasEvent ? ERROR_NONE : ERROR_TIMEOUT;
}

}
