// SPDX-License-Identifier: Apache-2.0
#include <location/ip_provider.h>
#include <location/module.h>

#include <http/download.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/network.h>
#include <tactility/paths.h>
#include <tactility/system_event.h>
#include <tactility/time.h>

#include <tactility/Memory.hpp>
#include <tactility/std/string.hpp>

#include <cJSON.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string_view>

#define TAG "location_ip"

// The root certificate of the API's certificate chain
static const char* certificate_pem = "";

void location_configure_certificate(const char* pem) {
    certificate_pem = pem;
}

namespace {

constexpr auto* API_URL = "https://json.geoiplookup.io/";
constexpr uint64_t RETRY_INTERVAL_MS = 60 * 1000;
// Bounds the request rate when the network connects repeatedly
constexpr uint64_t MIN_INTERVAL_MS = 10 * 1000;
// Wake up interval to check whether the fallback became needed
constexpr uint32_t CHECK_INTERVAL_MS = 10 * 1000;
constexpr uint64_t LOOKUP_TIMEOUT_MS = 15 * 1000;
// Bounds the wait for the download, which doesn't stop when the provider is stopped
constexpr uint32_t LOOKUP_POLL_INTERVAL_MS = 100;
constexpr size_t RESPONSE_MAX_LENGTH = 2048;
// IP geolocation is accurate to roughly city level
constexpr float ACCURACY_METERS = 25000.0f;

/** A lookup result */
struct CachedLookup {
    // The network of the lookup, see trackedNetwork
    uint64_t network = 0;
    double latitude = NAN;
    double longitude = NAN;
};

struct IpProvider {
    LocationHost* host = nullptr;
    Thread* thread = nullptr;
    std::atomic<bool> running = false;
    TaskEventGroup group {};
    uint32_t stopBit = 0;
    SystemEventSubscription networkSubscription {};
};

// The last lookup result, kept across provider runs until the network isn't tracked anymore.
// Only accessed by the provider thread and by ip_provider_untrack_network() after the provider stopped.
CachedLookup* cache = nullptr;

// The local IP address and gateway of the last connected network, 0 when unknown.
// A disconnect keeps the last known network, so reconnecting to it doesn't need a new lookup.
std::atomic<uint64_t> trackedNetwork = 0;

void onNetworkConnected(SystemEvent* event, void*) {
    NetworkConnectedEvent connected;
    if (event->data_len < sizeof(connected)) {
        return;
    }
    memcpy(&connected, event->data, sizeof(connected));
    trackedNetwork = (static_cast<uint64_t>(connected.ipv4_addr) << 32) | connected.gateway;
}

uint64_t getUptimeMs() {
    return get_micros_since_boot() / 1000;
}

/**
 * Downloads the lookup result into a temp file named @a file_name and parses it
 * @param[in] group the event group to wait on, with a free bit for the download
 * @param[in] running the lookup is cancelled when this becomes false
 * @return false when the lookup failed or was cancelled
 */
bool fetchLookup(const char* file_name, TaskEventGroup* group, const std::atomic<bool>& running, IpLookupResponse* out) {
    char temp_path[FILE_MAX_PATH_STRING_LENGTH];
    char target_path[FILE_MAX_PATH_STRING_LENGTH];
    if (paths_get_temp_path(temp_path, sizeof(temp_path)) != ERROR_NONE) {
        return false;
    }
    if (!directory_exists(temp_path)) {
        const error_t error = directory_make(temp_path, true);
        if (error != ERROR_NONE && error != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    const int written = snprintf(target_path, sizeof(target_path), "%s/%s", temp_path, file_name);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(target_path)) {
        return false;
    }

    HttpDownloadSubscription subscription {};
    if (http_download_subscribe(&subscription, group) != ERROR_NONE) {
        return false;
    }
    if (http_download_start(API_URL, certificate_pem, target_path, &subscription) != ERROR_NONE) {
        http_download_unsubscribe(&subscription);
        return false;
    }
    const uint64_t deadline = getUptimeMs() + LOOKUP_TIMEOUT_MS;
    HttpDownloadEvent event {};
    bool finished = false;
    while (running && getUptimeMs() < deadline) {
        if (http_download_poll(&subscription, &event) == ERROR_NONE) {
            finished = true;
            break;
        }
        task_event_group_wait_any(group, nullptr, millis_to_ticks(LOOKUP_POLL_INTERVAL_MS));
    }
    if (!finished) {
        http_download_cancel(&subscription);
    }
    http_download_unsubscribe(&subscription);
    if (!finished || event.type != HTTP_DOWNLOAD_EVENT_SUCCESS) {
        if (finished && event.type == HTTP_DOWNLOAD_EVENT_ERROR) {
            LOG_W(TAG, "Request failed: %s (HTTP %d)", event.error.message, static_cast<int>(event.status_code));
        }
        return false;
    }

    tt::std::string response(RESPONSE_MAX_LENGTH, '\0');
    size_t size = response.size() - 1;
    const error_t error = file_read_binary(target_path, reinterpret_cast<uint8_t*>(response.data()), &size);
    remove(target_path);
    if (error != ERROR_NONE) {
        return false;
    }
    response[size] = '\0';
    if (!ip_lookup_parse_response(response.c_str(), out)) {
        LOG_W(TAG, "Malformed response");
        return false;
    }
    return true;
}

void report(IpProvider& provider, double latitude, double longitude) {
    Location location {
        .latitude = latitude,
        .longitude = longitude,
        .horizontal_accuracy = ACCURACY_METERS,
        .altitude = NAN,
        .ellipsoidal_altitude = NAN,
        .vertical_accuracy = NAN,
        .speed = NAN,
        .speed_accuracy = NAN,
        .course = NAN,
        .course_accuracy = NAN,
        .satellites = 0,
        .time_ms = 0,
        .uptime_ms = 0,
        .provider = {},
    };
    if (location_get_precision() == LOCATION_PRECISION_APPROXIMATE) {
        location_make_approximate(&location);
    }
    location_host_report(provider.host, &location);
}

/** @return the delay until the next lookup is allowed */
uint64_t lookup(IpProvider& provider, uint64_t network) {
    IpLookupResponse parsed;
    if (!fetchLookup("location_ip.json", &provider.group, provider.running, &parsed)) {
        return RETRY_INTERVAL_MS;
    }
    if (!parsed.success) {
        LOG_W(TAG, "Lookup failed");
        return RETRY_INTERVAL_MS;
    }
    if (cache == nullptr) {
        cache = tt::newObject<CachedLookup>();
    }
    if (cache != nullptr) {
        *cache = { .network = network, .latitude = parsed.latitude, .longitude = parsed.longitude };
    }
    report(provider, parsed.latitude, parsed.longitude);
    LOG_D(TAG, "Lookup succeeded");
    return MIN_INTERVAL_MS;
}

int32_t threadMain(void* context) {
    auto& provider = *static_cast<IpProvider*>(context);
    uint64_t lastAttemptMs = 0;
    uint64_t nextAttemptMs = 0;
    bool attempted = false;
    // Whether the cached result was reported during this run
    bool reportedCache = false;

    while (provider.running) {
        const uint64_t now = getUptimeMs();
        const uint64_t waitMs = std::min<uint64_t>(CHECK_INTERVAL_MS, nextAttemptMs > now ? nextAttemptMs - now : 0);
        uint32_t flags = 0;
        task_event_group_wait_any(&provider.group, &flags, millis_to_ticks(static_cast<uint32_t>(waitMs)));
        if (!provider.running) {
            break;
        }

        // The public IP address can change with the network
        if (system_event_poll(&provider.networkSubscription) == ERROR_NONE) {
            nextAttemptMs = attempted ? lastAttemptMs + MIN_INTERVAL_MS : 0;
        }

        if (!location_host_needs_fallback()) {
            nextAttemptMs = std::max(nextAttemptMs, getUptimeMs() + CHECK_INTERVAL_MS);
            continue;
        }

        const uint64_t network = trackedNetwork;
        if (cache != nullptr && cache->network == network) {
            if (!reportedCache) {
                report(provider, cache->latitude, cache->longitude);
                reportedCache = true;
            }
            nextAttemptMs = std::max(nextAttemptMs, getUptimeMs() + CHECK_INTERVAL_MS);
            continue;
        }

        if (!network_is_available()) {
            nextAttemptMs = getUptimeMs() + CHECK_INTERVAL_MS;
            continue;
        }

        const uint64_t attemptMs = getUptimeMs();
        if (attemptMs < nextAttemptMs) {
            continue;
        }
        attempted = true;
        lastAttemptMs = attemptMs;
        nextAttemptMs = attemptMs + lookup(provider, network);
        reportedCache = cache != nullptr && cache->network == network;
    }
    return 0;
}

void* create(LocationHost* host) {
    auto* provider = tt::newObject<IpProvider>();
    if (provider == nullptr) {
        return nullptr;
    }
    provider->host = host;
    provider->running = true;
    task_event_group_construct(&provider->group);
    if (task_event_group_claim_bit(&provider->group, &provider->stopBit) != ERROR_NONE) {
        task_event_group_destruct(&provider->group);
        tt::deleteObject(provider);
        return nullptr;
    }
    provider->networkSubscription.event.type = KERNEL_EVENT_NETWORK_CONNECTED;
    if (system_event_subscribe(&provider->networkSubscription, &provider->group) != ERROR_NONE) {
        task_event_group_destruct(&provider->group);
        tt::deleteObject(provider);
        return nullptr;
    }

    provider->thread = thread_alloc_full("location_ip", 4096, threadMain, provider, -1);
    if (provider->thread != nullptr) {
        // Settings are loaded before providers start, so the thread doesn't access flash
        thread_set_stack_external(provider->thread, true);
    }
    if (provider->thread == nullptr || thread_start(provider->thread) != ERROR_NONE) {
        if (provider->thread != nullptr) {
            thread_free(provider->thread);
        }
        system_event_unsubscribe(&provider->networkSubscription);
        task_event_group_destruct(&provider->group);
        tt::deleteObject(provider);
        return nullptr;
    }
    return provider;
}

void requestStop(void* context) {
    auto* provider = static_cast<IpProvider*>(context);
    provider->running = false;
    task_event_group_signal(&provider->group, provider->stopBit);
}

bool waitStopped(void* context, TickType_t timeout) {
    auto* provider = static_cast<IpProvider*>(context);
    if (thread_join(provider->thread, timeout, millis_to_ticks(10)) != ERROR_NONE) {
        return false;
    }
    thread_free(provider->thread);
    system_event_unsubscribe(&provider->networkSubscription);
    task_event_group_destruct(&provider->group);
    tt::deleteObject(provider);
    return true;
}

/** The characters of IANA time zone names, e.g. "America/Port-au-Prince" or "Etc/GMT+5" */
bool isTimeZoneCharacter(char character) {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
        character == '_' || character == '+' || character == '-' || character == '/';
}

} // namespace

error_t ip_provider_track_network(void) {
    return system_event_callback_add(KERNEL_EVENT_NETWORK_CONNECTED, onNetworkConnected, nullptr);
}

void ip_provider_untrack_network(void) {
    system_event_callback_remove(KERNEL_EVENT_NETWORK_CONNECTED, onNetworkConnected);
    if (cache != nullptr) {
        tt::deleteObject(cache);
        cache = nullptr;
    }
}

error_t location_lookup_time_zone(char* out_name, size_t out_size) {
    if (!location_is_enabled()) {
        return ERROR_INVALID_STATE;
    }
    if (!network_is_available()) {
        return ERROR_RESOURCE;
    }
    TaskEventGroup group {};
    task_event_group_construct(&group);
    const std::atomic<bool> running = true;
    IpLookupResponse parsed;
    const bool fetched = fetchLookup("location_time_zone.json", &group, running, &parsed);
    task_event_group_destruct(&group);
    if (!fetched || !parsed.success) {
        return ERROR_RESOURCE;
    }
    const size_t length = strlen(parsed.time_zone);
    if (length == 0) {
        return ERROR_NOT_FOUND;
    }
    if (length >= out_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    memcpy(out_name, parsed.time_zone, length + 1);
    return ERROR_NONE;
}

bool ip_lookup_parse_response(const char* response, IpLookupResponse* out) {
    *out = IpLookupResponse {
        .success = false,
        .latitude = NAN,
        .longitude = NAN,
        .time_zone = {},
    };

    cJSON* json = cJSON_Parse(response);
    if (!cJSON_IsObject(json)) {
        cJSON_Delete(json);
        return false;
    }
    bool valid = false;
    const cJSON* success = cJSON_GetObjectItemCaseSensitive(json, "success");
    if (cJSON_IsFalse(success)) {
        valid = true;
    } else if (cJSON_IsTrue(success)) {
        const cJSON* latitude = cJSON_GetObjectItemCaseSensitive(json, "latitude");
        const cJSON* longitude = cJSON_GetObjectItemCaseSensitive(json, "longitude");
        const cJSON* timeZone = cJSON_GetObjectItemCaseSensitive(json, "timezone_name");
        if (cJSON_IsNumber(latitude) && cJSON_IsNumber(longitude) &&
            std::isfinite(latitude->valuedouble) && std::isfinite(longitude->valuedouble) &&
            std::fabs(latitude->valuedouble) <= 90.0 && std::fabs(longitude->valuedouble) <= 180.0) {
            const std::string_view name = cJSON_IsString(timeZone) ? timeZone->valuestring : "";
            if (name.size() <= LOCATION_TIME_ZONE_MAX_LENGTH && std::ranges::all_of(name, isTimeZoneCharacter)) {
                out->latitude = latitude->valuedouble;
                out->longitude = longitude->valuedouble;
                memcpy(out->time_zone, name.data(), name.size());
                out->time_zone[name.size()] = '\0';
                out->success = true;
                valid = true;
            }
        }
    }
    cJSON_Delete(json);
    return valid;
}

const LocationProviderApi ip_location_provider = {
    .id = "ip",
    .fallback = true,
    .provides_time = false,
    .create = create,
    .request_stop = requestStop,
    .wait_stopped = waitStopped,
};
