// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/radio.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/time.h>

#include <algorithm>
#include <cstring>
#include <vector>

#define RADIO_API(device) ((const struct RadioApi*)device_get_driver(device)->api)

struct RadioEventMutex {
    Mutex handle {};
    RadioEventMutex() { mutex_construct(&handle); }
    ~RadioEventMutex() { mutex_destruct(&handle); }
};

// Each event kind gets its own intrusive singly-linked subscription list, keyed by device, and
// its own coarse-grained mutex. Notifying a subscriber never invokes caller code (just a struct
// copy and a task_event_group_signal), so there is no reentrancy concern requiring a
// snapshot-then-unlock dance.
static RadioStateEventSubscription* radio_state_event_subscriptions = nullptr;
static RadioEventMutex radio_state_event_subscriptions_mutex;

static RadioRxEventSubscription* radio_rx_event_subscriptions = nullptr;
static RadioEventMutex radio_rx_event_subscriptions_mutex;

static RadioTxEventSubscription* radio_tx_event_subscriptions = nullptr;
static RadioEventMutex radio_tx_event_subscriptions_mutex;

// Kernel-side per-device state that drivers don't need to know about
struct RadioDeviceState {
    Device* device;
    char owner[RADIO_OWNER_NAME_MAX_LENGTH + 1];
    RadioRegion region;
};

static std::vector<RadioDeviceState> radio_device_states;
static RadioEventMutex radio_device_states_mutex;

static RadioDeviceState* find_device_state(Device* device) {
    for (auto& state : radio_device_states) {
        if (state.device == device) {
            return &state;
        }
    }
    return nullptr;
}

static RadioDeviceState& get_or_add_device_state(Device* device) {
    auto* state = find_device_state(device);
    if (state != nullptr) {
        return *state;
    }
    radio_device_states.push_back(RadioDeviceState { .device = device, .owner = {}, .region = RADIO_REGION_UNSET });
    return radio_device_states.back();
}

static void remove_device_state_if_unused(Device* device) {
    std::erase_if(radio_device_states, [device](const RadioDeviceState& state) {
        return state.device == device && state.owner[0] == '\0' && state.region == RADIO_REGION_UNSET;
    });
}

static constexpr RadioRegionBand REGION_BANDS_EU_433[] = { { 433050000, 434790000, 10 } };
static constexpr RadioRegionBand REGION_BANDS_EU_868[] = {
    { 863000000, 869400000, 14 },
    { 869400000, 869650000, 27 },
    { 869650000, 870000000, 14 },
};
static constexpr RadioRegionBand REGION_BANDS_US_915[] = { { 902000000, 928000000, 30 } };
static constexpr RadioRegionBand REGION_BANDS_AU_915[] = { { 915000000, 928000000, 30 } };
static constexpr RadioRegionBand REGION_BANDS_NZ_865[] = { { 864000000, 868000000, 36 } };
static constexpr RadioRegionBand REGION_BANDS_IN_865[] = { { 865000000, 867000000, 30 } };
static constexpr RadioRegionBand REGION_BANDS_RU_868[] = { { 868700000, 869200000, 20 } };
static constexpr RadioRegionBand REGION_BANDS_CN_470[] = { { 470000000, 510000000, 19 } };
static constexpr RadioRegionBand REGION_BANDS_JP_920[] = { { 920800000, 927800000, 16 } };

struct RadioRegionInfo {
    const char* name;
    const RadioRegionBand* bands;
    size_t band_count;
};

#define REGION_INFO(name, bands) { name, bands, sizeof(bands) / sizeof(bands[0]) }

static constexpr RadioRegionInfo REGION_INFOS[RADIO_REGION_COUNT] = {
    { "UNSET", nullptr, 0 },
    REGION_INFO("EU_433", REGION_BANDS_EU_433),
    REGION_INFO("EU_868", REGION_BANDS_EU_868),
    REGION_INFO("US_915", REGION_BANDS_US_915),
    REGION_INFO("AU_915", REGION_BANDS_AU_915),
    REGION_INFO("NZ_865", REGION_BANDS_NZ_865),
    REGION_INFO("IN_865", REGION_BANDS_IN_865),
    REGION_INFO("RU_868", REGION_BANDS_RU_868),
    REGION_INFO("CN_470", REGION_BANDS_CN_470),
    REGION_INFO("JP_920", REGION_BANDS_JP_920),
};

static bool is_in_any_band(enum RadioRegion region, int32_t frequency) {
    const RadioRegionBand* bands;
    const size_t count = radio_get_region_bands(region, &bands);
    for (size_t i = 0; i < count; i++) {
        if (frequency >= bands[i].frequency_min && frequency <= bands[i].frequency_max) {
            return true;
        }
    }
    return false;
}

static int8_t get_highest_power(enum RadioRegion region) {
    const RadioRegionBand* bands;
    const size_t count = radio_get_region_bands(region, &bands);
    int8_t highest = INT8_MIN;
    for (size_t i = 0; i < count; i++) {
        highest = std::max(highest, bands[i].power_max);
    }
    return highest;
}

/** Regions limit the sub-GHz bands. ESP-NOW is limited by the Wi-Fi stack, which knows the country. */
static bool is_limited_by_region(Device* device) {
    enum RadioModulation modulation = RADIO_MODULATION_NONE;
    return RADIO_API(device)->get_modulation(device, &modulation) != ERROR_NONE || modulation != RADIO_MODULATION_ESPNOW;
}

/** Check the configured frequency, bandwidth and power against a single band of the region */
static error_t check_configuration_in_region(Device* device, enum RadioRegion region) {
    int32_t frequency;
    if (RADIO_API(device)->get_parameter(device, RADIO_PARAMETER_FREQUENCY, &frequency) != ERROR_NONE) {
        // A frequency that can't be read can't be verified
        return ERROR_OUT_OF_RANGE;
    }

    int32_t bandwidth = 0;
    RADIO_API(device)->get_parameter(device, RADIO_PARAMETER_BANDWIDTH, &bandwidth);
    const int32_t half_bandwidth = bandwidth / 2;

    int32_t power;
    const bool has_power = RADIO_API(device)->get_parameter(device, RADIO_PARAMETER_POWER, &power) == ERROR_NONE;

    const RadioRegionBand* bands;
    const size_t count = radio_get_region_bands(region, &bands);
    for (size_t i = 0; i < count; i++) {
        if ((frequency - half_bandwidth) >= bands[i].frequency_min &&
            (frequency + half_bandwidth) <= bands[i].frequency_max &&
            (!has_power || power <= bands[i].power_max)) {
            return ERROR_NONE;
        }
    }
    return ERROR_OUT_OF_RANGE;
}

extern "C" {

// region State events

error_t radio_state_event_subscribe(RadioStateEventSubscription* sub, TaskEventGroup* event_group, Device* device) {
    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&radio_state_event_subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever
    for (RadioStateEventSubscription* existing = radio_state_event_subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&radio_state_event_subscriptions_mutex.handle);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->bit = bit;
    sub->internal.device = device;
    sub->internal.event_group = event_group;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = radio_state_event_subscriptions;
    radio_state_event_subscriptions = sub;
    mutex_unlock(&radio_state_event_subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t radio_state_event_unsubscribe(RadioStateEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&radio_state_event_subscriptions_mutex.handle);
    for (RadioStateEventSubscription** link = &radio_state_event_subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&radio_state_event_subscriptions_mutex.handle);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }

    return result;
}

error_t radio_state_event_emit(Device* device, enum RadioState state) {
    RadioStateEvent stamped_event { .timestamp = get_micros_since_boot(), .state = state };

    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&radio_state_event_subscriptions_mutex.handle);
    for (RadioStateEventSubscription* sub = radio_state_event_subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }

        if (sub->internal.count >= RADIO_STATE_EVENT_QUEUE_CAPACITY) {
            result = ERROR_RESOURCE;
            continue;
        }

        uint8_t tail = (sub->internal.head + sub->internal.count) % RADIO_STATE_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = stamped_event;
        sub->internal.count++;
        if (result != ERROR_RESOURCE) {
            result = ERROR_NONE;
        }
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&radio_state_event_subscriptions_mutex.handle);

    return result;
}

error_t radio_state_event_poll(RadioStateEventSubscription* sub, RadioStateEvent* out_event) {
    mutex_lock(&radio_state_event_subscriptions_mutex.handle);
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % RADIO_STATE_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&radio_state_event_subscriptions_mutex.handle);
    return has_event ? ERROR_NONE : ERROR_TIMEOUT;
}

// endregion

// region RX events

error_t radio_rx_event_subscribe(RadioRxEventSubscription* sub, TaskEventGroup* event_group, Device* device) {
    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&radio_rx_event_subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever
    for (RadioRxEventSubscription* existing = radio_rx_event_subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&radio_rx_event_subscriptions_mutex.handle);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->bit = bit;
    sub->internal.device = device;
    sub->internal.event_group = event_group;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = radio_rx_event_subscriptions;
    radio_rx_event_subscriptions = sub;
    mutex_unlock(&radio_rx_event_subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t radio_rx_event_unsubscribe(RadioRxEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&radio_rx_event_subscriptions_mutex.handle);
    for (RadioRxEventSubscription** link = &radio_rx_event_subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&radio_rx_event_subscriptions_mutex.handle);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }

    return result;
}

error_t radio_rx_event_emit(Device* device, const uint8_t* data, size_t length, float rssi, float snr) {
    RadioRxEvent stamped_event {};
    stamped_event.timestamp = get_micros_since_boot();
    stamped_event.length = std::min(length, sizeof(stamped_event.data));
    std::memcpy(stamped_event.data, data, stamped_event.length);
    stamped_event.rssi = rssi;
    stamped_event.snr = snr;

    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&radio_rx_event_subscriptions_mutex.handle);
    for (RadioRxEventSubscription* sub = radio_rx_event_subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }

        if (sub->internal.count >= RADIO_RX_EVENT_QUEUE_CAPACITY) {
            result = ERROR_RESOURCE;
            continue;
        }

        uint8_t tail = (sub->internal.head + sub->internal.count) % RADIO_RX_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = stamped_event;
        sub->internal.count++;
        if (result != ERROR_RESOURCE) {
            result = ERROR_NONE;
        }
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&radio_rx_event_subscriptions_mutex.handle);

    return result;
}

error_t radio_rx_event_poll(RadioRxEventSubscription* sub, RadioRxEvent* out_event) {
    mutex_lock(&radio_rx_event_subscriptions_mutex.handle);
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % RADIO_RX_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&radio_rx_event_subscriptions_mutex.handle);
    return has_event ? ERROR_NONE : ERROR_TIMEOUT;
}

// endregion

// region TX events

error_t radio_tx_event_subscribe(RadioTxEventSubscription* sub, TaskEventGroup* event_group, Device* device) {
    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&radio_tx_event_subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever
    for (RadioTxEventSubscription* existing = radio_tx_event_subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&radio_tx_event_subscriptions_mutex.handle);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->bit = bit;
    sub->internal.device = device;
    sub->internal.event_group = event_group;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = radio_tx_event_subscriptions;
    radio_tx_event_subscriptions = sub;
    mutex_unlock(&radio_tx_event_subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t radio_tx_event_unsubscribe(RadioTxEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&radio_tx_event_subscriptions_mutex.handle);
    for (RadioTxEventSubscription** link = &radio_tx_event_subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&radio_tx_event_subscriptions_mutex.handle);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }

    return result;
}

error_t radio_tx_event_emit(Device* device, RadioTxId id, enum RadioTransmissionState state) {
    RadioTxEvent stamped_event { .timestamp = get_micros_since_boot(), .id = id, .state = state };

    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&radio_tx_event_subscriptions_mutex.handle);
    for (RadioTxEventSubscription* sub = radio_tx_event_subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }

        if (sub->internal.count >= RADIO_TX_EVENT_QUEUE_CAPACITY) {
            result = ERROR_RESOURCE;
            continue;
        }

        uint8_t tail = (sub->internal.head + sub->internal.count) % RADIO_TX_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = stamped_event;
        sub->internal.count++;
        if (result != ERROR_RESOURCE) {
            result = ERROR_NONE;
        }
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&radio_tx_event_subscriptions_mutex.handle);

    return result;
}

error_t radio_tx_event_poll(RadioTxEventSubscription* sub, RadioTxEvent* out_event) {
    mutex_lock(&radio_tx_event_subscriptions_mutex.handle);
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % RADIO_TX_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&radio_tx_event_subscriptions_mutex.handle);
    return has_event ? ERROR_NONE : ERROR_TIMEOUT;
}

// endregion

error_t radio_get_state(struct Device* device, enum RadioState* state) {
    return RADIO_API(device)->get_radio_state(device, state);
}

error_t radio_set_enabled(struct Device* device, bool enabled) {
    if (enabled) {
        const auto region = radio_get_region(device);
        if (region != RADIO_REGION_UNSET && is_limited_by_region(device)) {
            const error_t result = check_configuration_in_region(device, region);
            if (result != ERROR_NONE) {
                return result;
            }
        }
    }
    return RADIO_API(device)->set_enabled(device, enabled);
}

error_t radio_set_modulation(struct Device* device, enum RadioModulation modulation) {
    return RADIO_API(device)->set_modulation(device, modulation);
}

error_t radio_get_modulation(struct Device* device, enum RadioModulation* modulation) {
    return RADIO_API(device)->get_modulation(device, modulation);
}

bool radio_can_transmit(struct Device* device, enum RadioModulation modulation) {
    return RADIO_API(device)->can_transmit(device, modulation);
}

bool radio_can_receive(struct Device* device, enum RadioModulation modulation) {
    return RADIO_API(device)->can_receive(device, modulation);
}

error_t radio_set_parameter(struct Device* device, enum RadioParameter parameter, int32_t value) {
    const auto region = radio_get_region(device);
    if (region != RADIO_REGION_UNSET && is_limited_by_region(device)) {
        if (parameter == RADIO_PARAMETER_FREQUENCY && !is_in_any_band(region, value)) {
            return ERROR_OUT_OF_RANGE;
        }
        if (parameter == RADIO_PARAMETER_POWER && value > get_highest_power(region)) {
            return ERROR_OUT_OF_RANGE;
        }
    }
    return RADIO_API(device)->set_parameter(device, parameter, value);
}

error_t radio_get_parameter(struct Device* device, enum RadioParameter parameter, int32_t* value) {
    return RADIO_API(device)->get_parameter(device, parameter, value);
}

error_t radio_transmit(struct Device* device, const uint8_t* data, size_t length, RadioTxId* id) {
    return RADIO_API(device)->transmit(device, data, length, id);
}

error_t radio_get_rssi(struct Device* device, float* rssi) {
    const auto* api = RADIO_API(device);
    if (api->get_rssi == nullptr) return ERROR_NOT_SUPPORTED;
    return api->get_rssi(device, rssi);
}

error_t radio_is_receiving(struct Device* device, bool* receiving) {
    const auto* api = RADIO_API(device);
    if (api->is_receiving == nullptr) return ERROR_NOT_SUPPORTED;
    return api->is_receiving(device, receiving);
}

error_t radio_set_antenna(struct Device* device, uint32_t antenna) {
    const auto* api = RADIO_API(device);
    if (api->set_antenna == nullptr) return ERROR_NOT_SUPPORTED;
    return api->set_antenna(device, antenna);
}

error_t radio_get_antenna(struct Device* device, uint32_t* antenna) {
    const auto* api = RADIO_API(device);
    if (api->get_antenna == nullptr) return ERROR_NOT_SUPPORTED;
    return api->get_antenna(device, antenna);
}

// region Ownership

error_t radio_acquire(struct Device* device, const char* owner) {
    mutex_lock(&radio_device_states_mutex.handle);
    auto& state = get_or_add_device_state(device);
    error_t result;
    if (state.owner[0] == '\0') {
        std::strncpy(state.owner, owner, RADIO_OWNER_NAME_MAX_LENGTH);
        state.owner[RADIO_OWNER_NAME_MAX_LENGTH] = '\0';
        result = ERROR_NONE;
    } else if (std::strncmp(state.owner, owner, RADIO_OWNER_NAME_MAX_LENGTH) == 0) {
        result = ERROR_NONE;
    } else {
        result = ERROR_RESOURCE_BUSY;
    }
    mutex_unlock(&radio_device_states_mutex.handle);
    return result;
}

error_t radio_release(struct Device* device) {
    mutex_lock(&radio_device_states_mutex.handle);
    auto* state = find_device_state(device);
    error_t result = ERROR_INVALID_STATE;
    if (state != nullptr && state->owner[0] != '\0') {
        state->owner[0] = '\0';
        remove_device_state_if_unused(device);
        result = ERROR_NONE;
    }
    mutex_unlock(&radio_device_states_mutex.handle);
    return result;
}

error_t radio_get_owner(struct Device* device, char* owner, size_t owner_size) {
    mutex_lock(&radio_device_states_mutex.handle);
    auto* state = find_device_state(device);
    error_t result = ERROR_NOT_FOUND;
    if (state != nullptr && state->owner[0] != '\0' && owner_size > 0) {
        std::strncpy(owner, state->owner, owner_size - 1);
        owner[owner_size - 1] = '\0';
        result = ERROR_NONE;
    }
    mutex_unlock(&radio_device_states_mutex.handle);
    return result;
}

// endregion

// region Regulatory region

error_t radio_set_region(struct Device* device, enum RadioRegion region) {
    if (region < RADIO_REGION_UNSET || region >= RADIO_REGION_COUNT) {
        return ERROR_INVALID_ARGUMENT;
    }
    mutex_lock(&radio_device_states_mutex.handle);
    get_or_add_device_state(device).region = region;
    remove_device_state_if_unused(device);
    mutex_unlock(&radio_device_states_mutex.handle);
    return ERROR_NONE;
}

enum RadioRegion radio_get_region(struct Device* device) {
    mutex_lock(&radio_device_states_mutex.handle);
    auto* state = find_device_state(device);
    const auto region = (state != nullptr) ? state->region : RADIO_REGION_UNSET;
    mutex_unlock(&radio_device_states_mutex.handle);
    return region;
}

size_t radio_get_region_bands(enum RadioRegion region, const struct RadioRegionBand** bands) {
    if (region <= RADIO_REGION_UNSET || region >= RADIO_REGION_COUNT) {
        *bands = nullptr;
        return 0;
    }
    *bands = REGION_INFOS[region].bands;
    return REGION_INFOS[region].band_count;
}

const char* radio_region_to_string(enum RadioRegion region) {
    if (region < RADIO_REGION_UNSET || region >= RADIO_REGION_COUNT) {
        return "unknown";
    }
    return REGION_INFOS[region].name;
}

error_t radio_region_from_string(const char* name, enum RadioRegion* region) {
    for (int i = RADIO_REGION_UNSET; i < RADIO_REGION_COUNT; i++) {
        if (strcmp(REGION_INFOS[i].name, name) == 0) {
            *region = static_cast<RadioRegion>(i);
            return ERROR_NONE;
        }
    }
    return ERROR_NOT_FOUND;
}

// endregion

const struct DeviceType RADIO_TYPE = {
    .name = "radio"
};

} // extern "C"
