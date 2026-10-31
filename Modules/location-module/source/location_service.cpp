// SPDX-License-Identifier: Apache-2.0
#include <location/location.h>
#include <location/location_service.h>
#include <location/location_settings.h>
#include <location/provider.h>

#include <service/manager.h>
#include <service/manifest.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/delay.h>
#include <tactility/log.h>
#include <tactility/system_event.h>
#include <tactility/time.h>

#include <tactility/Memory.hpp>

#include <cmath>
#include <cstring>
#include <span>

#define TAG "location"

enum class HostState {
    Stopped,
    Running,
    Stopping,
};

struct LocationHost {
    const LocationProviderApi* api;
    void* provider;
    HostState state;
    // The worker is calling the provider without holding the mutex
    bool busy;
    // Unregistering, which waits for the provider to stop
    bool removing;
    // Not created again until the provider is no longer desired
    bool createFailed;

    bool fallback;
    bool hasLocation;
    Location location;
};

namespace {

/** A fix of a provider that isn't a fallback is preferred over fallback fixes for this long */
constexpr uint64_t FRESH_MS = 120000;
constexpr uint64_t TIME_EVENT_INTERVAL_MS = 60000;
// About 2 km north to south
constexpr double APPROXIMATE_GRID_DEGREES = 0.02;
// The worker waits this long for a stopping provider before it handles the other providers
constexpr uint32_t STOP_WAIT_MS = 100;
constexpr uint32_t UNREGISTER_POLL_MS = 10;
constexpr size_t WORKER_STACK_SIZE = 4096;

struct GlobalMutex {
    Mutex handle {};
    GlobalMutex() { mutex_construct(&handle); }
    ~GlobalMutex() { mutex_destruct(&handle); }
};

// Not held while calling provider functions
GlobalMutex mutex;

struct HostStorage {
    LocationHost hosts[LOCATION_PROVIDER_MAX_COUNT] {};
};

// Allocated in external memory when available, while providers are registered
HostStorage* hostStorage = nullptr;
bool running = false;
LocationSubscription* subscriptions = nullptr;
bool hasEmittedTime = false;
uint64_t lastTimeEventUptimeMs = 0;
// Starts and stops providers. It only exists while a provider isn't in its desired state.
Thread* worker = nullptr;
bool workerActive = false;

// 2025-01-01 and 2100-01-01 in milliseconds since the Unix epoch. GPS receivers with a wrong date, e.g. from a week rollover, fall outside it.
constexpr int64_t MIN_PLAUSIBLE_TIME_MS = 1735689600000;
constexpr int64_t MAX_PLAUSIBLE_TIME_MS = 4102444800000;

bool isPlausibleTime(int64_t timeMs) {
    return timeMs >= MIN_PLAUSIBLE_TIME_MS && timeMs < MAX_PLAUSIBLE_TIME_MS;
}

GlobalMutex settingsMutex;
LocationSettings settings {};
bool settingsLoaded = false;

/** Must be called with settingsMutex held */
void ensureSettingsLoaded() {
    if (!settingsLoaded) {
        location_settings_load(&settings);
        settingsLoaded = true;
    }
}

/** Must be called with mutex held */
std::span<LocationHost> getHosts() {
    return hostStorage != nullptr ? std::span<LocationHost>(hostStorage->hosts) : std::span<LocationHost>();
}

/** Frees the hosts when no provider is registered and the worker doesn't use them. Must be called with mutex held. */
void freeUnusedHosts() {
    if (hostStorage == nullptr || workerActive) {
        return;
    }
    for (const auto& host : hostStorage->hosts) {
        if (host.api != nullptr) {
            return;
        }
    }
    tt::deleteObject(hostStorage);
    hostStorage = nullptr;
}

uint64_t getUptimeMs() {
    return get_micros_since_boot() / 1000;
}

/** Must be called with mutex held */
int getRank(const LocationHost& host, uint64_t now) {
    if (host.fallback) {
        return 1;
    }
    return now - host.location.uptime_ms < FRESH_MS ? 2 : 0;
}

/** Must be called with mutex held */
const LocationHost* findBest() {
    const uint64_t now = getUptimeMs();
    const LocationHost* best = nullptr;
    int bestRank = -1;
    for (const auto& host : getHosts()) {
        if (!host.hasLocation) {
            continue;
        }
        const int rank = getRank(host, now);
        if (rank > bestRank || (rank == bestRank && host.location.uptime_ms > best->location.uptime_ms)) {
            best = &host;
            bestRank = rank;
        }
    }
    return best;
}

/** Must be called with mutex held */
bool isDesired(const LocationHost& host) {
    return host.api != nullptr && !host.removing && running && subscriptions != nullptr;
}

/** Must be called with mutex held */
bool isSettled(const LocationHost& host) {
    switch (host.state) {
        case HostState::Running:
            return isDesired(host);
        case HostState::Stopped:
            return !isDesired(host) || host.createFailed;
        case HostState::Stopping:
            return false;
    }
    return true;
}

/** Takes one step towards the desired state. Must be called with mutex held, which is released during the step. */
void step(LocationHost& host) {
    const LocationProviderApi* api = host.api;
    void* provider = host.provider;
    const HostState state = host.state;
    host.busy = true;
    mutex_unlock(&mutex.handle);

    switch (state) {
        case HostState::Stopped: {
            void* created = api->create(&host);
            mutex_lock(&mutex.handle);
            if (created != nullptr) {
                host.provider = created;
                host.state = HostState::Running;
            } else {
                LOG_E(TAG, "Failed to start provider %s", api->id);
                host.createFailed = true;
            }
            break;
        }
        case HostState::Running:
            api->request_stop(provider);
            mutex_lock(&mutex.handle);
            host.state = HostState::Stopping;
            break;
        case HostState::Stopping: {
            const bool stopped = api->wait_stopped(provider, millis_to_ticks(STOP_WAIT_MS));
            mutex_lock(&mutex.handle);
            if (stopped) {
                host.provider = nullptr;
                host.state = HostState::Stopped;
            }
            break;
        }
    }
    host.busy = false;
}

int32_t workerMain(void*) {
    mutex_lock(&mutex.handle);
    bool settled = false;
    while (!settled) {
        settled = true;
        for (auto& host : getHosts()) {
            if (!host.busy && !isSettled(host)) {
                settled = false;
                step(host);
            }
        }
    }
    workerActive = false;
    freeUnusedHosts();
    mutex_unlock(&mutex.handle);
    return 0;
}

/** Frees the worker after it exited. Must be called with mutex held. */
void freeFinishedWorker() {
    if (worker != nullptr && !workerActive) {
        // The worker released the mutex for the last time, so it exits right away
        thread_join(worker, portMAX_DELAY, 1);
        thread_free(worker);
        worker = nullptr;
    }
}

/** Starts the worker when a provider isn't in its desired state. Must be called with mutex held. */
void reconcile() {
    bool settled = true;
    for (auto& host : getHosts()) {
        if (!isDesired(host)) {
            host.createFailed = false;
        }
        settled = settled && isSettled(host);
    }
    if (settled || workerActive) {
        return;
    }

    // The worker doesn't access flash, so its stack can be in external memory
    mutex_lock(&settingsMutex.handle);
    ensureSettingsLoaded();
    mutex_unlock(&settingsMutex.handle);

    freeFinishedWorker();
    worker = thread_alloc_full("location", WORKER_STACK_SIZE, workerMain, nullptr, -1);
    if (worker == nullptr) {
        LOG_E(TAG, "Failed to allocate worker");
        return;
    }
    thread_set_stack_external(worker, true);
    if (thread_start(worker) != ERROR_NONE) {
        LOG_E(TAG, "Failed to start worker");
        thread_free(worker);
        worker = nullptr;
        return;
    }
    workerActive = true;
}

void* createService(const ServiceManifest*) {
    return nullptr;
}

void destroyService(const ServiceManifest*, void*) {}

error_t onStart(ServiceInstance*, void*) {
    mutex_lock(&mutex.handle);
    running = true;
    hasEmittedTime = false;
    reconcile();
    mutex_unlock(&mutex.handle);
    return ERROR_NONE;
}

void onStop(ServiceInstance*, void*) {
    mutex_lock(&mutex.handle);
    running = false;
    for (auto& host : getHosts()) {
        host.hasLocation = false;
    }
    reconcile();
    freeFinishedWorker();
    mutex_unlock(&mutex.handle);
}

} // namespace

extern "C" {

error_t location_set_enabled(bool enabled) {
    mutex_lock(&settingsMutex.handle);
    ensureSettingsLoaded();
    settings.enabled = enabled;
    const LocationSettings copy = settings;
    mutex_unlock(&settingsMutex.handle);

    const error_t saved = location_settings_save(&copy);
    error_t result;
    if (enabled) {
        result = service_manager_start(LOCATION_SERVICE_ID);
        if (result == ERROR_INVALID_STATE) {
            result = ERROR_NONE;
        }
    } else {
        result = service_manager_stop(LOCATION_SERVICE_ID);
        if (result == ERROR_NOT_FOUND) {
            result = ERROR_NONE;
        }
    }
    return result != ERROR_NONE ? result : saved;
}

bool location_is_enabled(void) {
    mutex_lock(&settingsMutex.handle);
    ensureSettingsLoaded();
    const bool enabled = settings.enabled;
    mutex_unlock(&settingsMutex.handle);
    return enabled;
}

error_t location_set_precision(LocationPrecision precision) {
    mutex_lock(&settingsMutex.handle);
    ensureSettingsLoaded();
    settings.precision = precision;
    const LocationSettings copy = settings;
    mutex_unlock(&settingsMutex.handle);

    if (precision == LOCATION_PRECISION_APPROXIMATE) {
        // Precise fixes are dropped until their providers report again. Coarse fixes are rounded.
        mutex_lock(&mutex.handle);
        for (auto& host : getHosts()) {
            if (host.hasLocation) {
                if (host.location.horizontal_accuracy >= LOCATION_APPROXIMATE_ACCURACY_METERS) {
                    location_make_approximate(&host.location);
                } else {
                    host.hasLocation = false;
                }
            }
        }
        mutex_unlock(&mutex.handle);
    }
    return location_settings_save(&copy);
}

LocationPrecision location_get_precision(void) {
    mutex_lock(&settingsMutex.handle);
    ensureSettingsLoaded();
    const LocationPrecision precision = settings.precision;
    mutex_unlock(&settingsMutex.handle);
    return precision;
}

void location_make_approximate(Location* location) {
    location->latitude = std::round(location->latitude / APPROXIMATE_GRID_DEGREES) * APPROXIMATE_GRID_DEGREES;
    location->longitude = std::round(location->longitude / APPROXIMATE_GRID_DEGREES) * APPROXIMATE_GRID_DEGREES;
    if (!(location->horizontal_accuracy >= LOCATION_APPROXIMATE_ACCURACY_METERS)) {
        location->horizontal_accuracy = LOCATION_APPROXIMATE_ACCURACY_METERS;
    }
    location->altitude = NAN;
    location->ellipsoidal_altitude = NAN;
    location->vertical_accuracy = NAN;
    location->speed = NAN;
    location->speed_accuracy = NAN;
    location->course = NAN;
    location->course_accuracy = NAN;
    location->satellites = 0;
}

error_t location_provider_register(const LocationProviderApi* api) {
    if (strlen(api->id) > LOCATION_PROVIDER_ID_MAX_LENGTH) {
        return ERROR_INVALID_ARGUMENT;
    }

    mutex_lock(&mutex.handle);
    if (hostStorage == nullptr) {
        hostStorage = tt::newObject<HostStorage>();
        if (hostStorage == nullptr) {
            mutex_unlock(&mutex.handle);
            return ERROR_OUT_OF_MEMORY;
        }
    }
    LocationHost* freeHost = nullptr;
    for (auto& host : getHosts()) {
        if (host.api == nullptr) {
            if (freeHost == nullptr) {
                freeHost = &host;
            }
        } else if (strcmp(host.api->id, api->id) == 0) {
            mutex_unlock(&mutex.handle);
            return ERROR_ALREADY_EXISTS;
        }
    }
    if (freeHost == nullptr) {
        mutex_unlock(&mutex.handle);
        return ERROR_RESOURCE;
    }
    freeHost->api = api;
    freeHost->fallback = api->fallback;
    reconcile();
    mutex_unlock(&mutex.handle);
    return ERROR_NONE;
}

error_t location_provider_unregister(const LocationProviderApi* api) {
    mutex_lock(&mutex.handle);
    LocationHost* host = nullptr;
    for (auto& candidate : getHosts()) {
        if (candidate.api == api && !candidate.removing) {
            host = &candidate;
            break;
        }
    }
    if (host == nullptr) {
        mutex_unlock(&mutex.handle);
        return ERROR_NOT_FOUND;
    }

    host->removing = true;
    while (host->state != HostState::Stopped || host->busy) {
        reconcile();
        mutex_unlock(&mutex.handle);
        delay_millis(UNREGISTER_POLL_MS);
        mutex_lock(&mutex.handle);
    }
    host->hasLocation = false;
    host->api = nullptr;
    host->removing = false;
    host->createFailed = false;
    freeUnusedHosts();
    mutex_unlock(&mutex.handle);
    return ERROR_NONE;
}

void location_host_report(LocationHost* host, const Location* location) {
    mutex_lock(&mutex.handle);
    // Providers that are being stopped don't report anymore
    if (host->state != HostState::Running || !isDesired(*host)) {
        mutex_unlock(&mutex.handle);
        return;
    }
    host->location = *location;
    strcpy(host->location.provider, host->api->id);
    host->location.uptime_ms = getUptimeMs();
    host->hasLocation = true;
    const bool isBest = findBest() == host;
    if (isBest) {
        for (auto* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
            task_event_group_signal(sub->internal.event_group, sub->bit);
        }
    }
    const uint64_t now = host->location.uptime_ms;
    const bool emitTime = isBest && host->api->provides_time && isPlausibleTime(host->location.time_ms) &&
        (!hasEmittedTime || now - lastTimeEventUptimeMs >= TIME_EVENT_INTERVAL_MS);
    if (emitTime) {
        hasEmittedTime = true;
        lastTimeEventUptimeMs = now;
    }
    const LocationTimeEvent timeEvent = { .time_ms = host->location.time_ms };
    mutex_unlock(&mutex.handle);
    if (emitTime) {
        system_event_emit(KERNEL_EVENT_LOCATION_TIME, &timeEvent, sizeof(timeEvent));
    }
}

bool location_host_needs_fallback(void) {
    mutex_lock(&mutex.handle);
    const uint64_t now = getUptimeMs();
    bool needed = true;
    for (const auto& host : getHosts()) {
        if (host.hasLocation && getRank(host, now) == 2) {
            needed = false;
            break;
        }
    }
    mutex_unlock(&mutex.handle);
    return needed;
}

error_t location_get(Location* location) {
    mutex_lock(&mutex.handle);
    const auto* best = findBest();
    if (best != nullptr) {
        *location = best->location;
    }
    mutex_unlock(&mutex.handle);
    return best != nullptr ? ERROR_NONE : ERROR_NOT_FOUND;
}

namespace {

/** Copies at most LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH bytes, without cutting a UTF-8 character in half */
void copyDescription(char* out, const char* description) {
    size_t length = strnlen(description, LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH + 1);
    if (length > LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH) {
        length = LOCATION_SUBSCRIBER_DESCRIPTION_MAX_LENGTH;
        // The first byte that doesn't fit must start a character, otherwise back up to the start of the cut one
        while (length > 0 && (static_cast<uint8_t>(description[length]) & 0xC0) == 0x80) {
            length--;
        }
    }
    memcpy(out, description, length);
    out[length] = '\0';
}

}

error_t location_subscribe(LocationSubscription* sub, TaskEventGroup* event_group, const char* description) {
    uint32_t bit;
    const error_t claimed = task_event_group_claim_bit(event_group, &bit);
    if (claimed != ERROR_NONE) {
        return claimed;
    }

    mutex_lock(&mutex.handle);
    for (auto* existing = subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&mutex.handle);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }
    sub->bit = bit;
    copyDescription(sub->internal.description, description != nullptr ? description : "");
    sub->internal.event_group = event_group;
    sub->internal.next = subscriptions;
    subscriptions = sub;
    reconcile();
    mutex_unlock(&mutex.handle);
    return ERROR_NONE;
}

error_t location_unsubscribe(LocationSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;
    mutex_lock(&mutex.handle);
    for (auto** link = &subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    reconcile();
    freeFinishedWorker();
    mutex_unlock(&mutex.handle);
    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }
    return result;
}

void location_for_each_subscriber(void* context, void (*callback)(const char* description, void* context)) {
    mutex_lock(&mutex.handle);
    for (auto* sub = subscriptions; sub != nullptr; sub = sub->internal.next) {
        callback(sub->internal.description, context);
    }
    mutex_unlock(&mutex.handle);
}

bool location_is_in_use(void) {
    mutex_lock(&mutex.handle);
    const bool inUse = running && subscriptions != nullptr;
    mutex_unlock(&mutex.handle);
    return inUse;
}
}

extern const ServiceManifest location_service_manifest = {
    .id = LOCATION_SERVICE_ID,
    .create_service = createService,
    .destroy_service = destroyService,
    .on_start = onStart,
    .on_stop = onStop,
};
