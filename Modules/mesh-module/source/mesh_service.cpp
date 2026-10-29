// SPDX-License-Identifier: Apache-2.0
#include <mesh/backend.h>
#include <mesh/mesh.h>
#include <mesh/mesh_airtime.h>
#include <mesh/mesh_service.h>
#include <mesh/mesh_settings.h>
#include <mesh/mesh_store.h>

#include <service/manager.h>
#include <service/manifest.h>
#include <service/paths.h>

#include <tactility/Memory.hpp>
#include <tactility/std/deque.hpp>
#include <tactility/std/vector.hpp>

#include <tactility/check.h>
#include <tactility/concurrent/event_group.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/concurrent/thread.h>
#include <tactility/device.h>
#include <tactility/filesystem/fs.h>
#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <algorithm>
#include <string>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <functional>

constexpr auto* TAG = "mesh";

namespace {

constexpr size_t THREAD_STACK_SIZE = 8192;
constexpr size_t MAX_BACKENDS = 4;
constexpr size_t MAX_NODES = 128;
constexpr size_t TX_QUEUE_LIMIT = 16;
constexpr uint64_t TICK_INTERVAL_MS = 1000;
constexpr uint64_t FLUSH_MAX_AGE_MS = 30000;
constexpr uint64_t DUTY_CYCLE_WINDOW_MS = 3600 * 1000;
constexpr int CHANNEL_BUSY_RETRIES = 3;
constexpr uint32_t CALL_DONE_BIT = (1 << 0);
// 2023-11-14: a system clock before this was never set
constexpr int64_t MIN_VALID_WALL_TIME = 1700000000;

struct GlobalMutex {
    Mutex handle {};
    GlobalMutex() { mutex_construct(&handle); }
    ~GlobalMutex() { mutex_destruct(&handle); }
};

/** Guards the backend registry, the settings, the event subscriptions and the service pointer with its data */
GlobalMutex globalMutex;

void lock() { mutex_lock(&globalMutex.handle); }
void unlock() { mutex_unlock(&globalMutex.handle); }

/** Serializes callOnServiceThread() callers. Taken before the global lock, never while holding it */
GlobalMutex callMutex;

/** The registry, settings and subscriptions. Allocated once in external memory and guarded by the global lock. */
struct Globals {
    const MeshBackendApi* backends[MAX_BACKENDS] = {};
    size_t backendCount = 0;
    MeshSettings settings {};
    bool settingsLoaded = false;
    /** Settings of the backends, loaded on first use */
    struct BackendSettingsEntry {
        const MeshBackendApi* api;
        MeshBackendSettings settings;
    } backendSettings[MAX_BACKENDS] = {};
    size_t backendSettingsCount = 0;
    bool legacySettingsMigrated = false;
    MeshEventSubscription* subscriptions = nullptr;
};

Globals& globals() {
    static Globals* instance = tt::newObject<Globals>();
    check(instance != nullptr);
    return *instance;
}

uint64_t getMillis() {
    return get_micros_since_boot() / 1000;
}

const MeshBackendApi* findBackend(const char* id) {
    for (size_t i = 0; i < globals().backendCount; i++) {
        if (strncmp(globals().backends[i]->id, id, MESH_BACKEND_ID_MAX_LENGTH) == 0) {
            return globals().backends[i];
        }
    }
    return nullptr;
}

void ensureSettingsLoaded();

bool canSendAndReceive(Device* device, RadioModulation modulation) {
    return radio_can_transmit(device, modulation) && radio_can_receive(device, modulation);
}

/** Whether the radio does LoRa. Other mesh radios do ESP-NOW. */
bool usesLora(Device* device) {
    return canSendAndReceive(device, RADIO_MODULATION_LORA);
}

/** A started radio that can be used for mesh networking */
bool isMeshRadio(Device* device) {
    return device_is_ready(device) && (usesLora(device) || canSendAndReceive(device, RADIO_MODULATION_ESPNOW));
}

/**
 * Takes a reference on the selected radio. Without a selection, or when the selected radio isn't there,
 * that is the first radio that isn't ESP-NOW, or the first ESP-NOW radio when there is no other.
 * @return nullptr when there is none
 */
Device* findRadio() {
    char name[MESH_NAME_MAX_LENGTH + 1];
    lock();
    ensureSettingsLoaded();
    strncpy(name, globals().settings.radio, sizeof(name));
    name[MESH_NAME_MAX_LENGTH] = '\0';
    unlock();

    struct Search {
        const char* name;
        Device* selected = nullptr;
        Device* lora = nullptr;
        Device* other = nullptr;
    } search { name };
    device_for_each_of_type(&RADIO_TYPE, &search, [](Device* device, void* context) {
        auto* search = static_cast<Search*>(context);
        if (!isMeshRadio(device)) {
            return true;
        }
        if (search->name[0] != '\0' && strcmp(device->name, search->name) == 0) {
            search->selected = device;
            return false;
        }
        Device*& slot = usesLora(device) ? search->lora : search->other;
        if (slot == nullptr) {
            slot = device;
        }
        return true;
    });

    Device* radio = search.selected != nullptr ? search.selected : (search.lora != nullptr ? search.lora : search.other);
    if (radio != nullptr && device_get(radio) != ERROR_NONE) {
        return nullptr;
    }
    return radio;
}

/** Whether the presets of the backend apply to the radio that is used: they are for LoRa */
bool presetsApply() {
    Device* radio = findRadio();
    if (radio == nullptr) {
        return false;
    }
    const bool applies = usesLora(radio);
    device_put(radio);
    return applies;
}

/** Requires the global lock and loaded settings */
const MeshBackendApi* getSelectedBackend() {
    if (globals().settings.backend[0] != '\0') {
        return findBackend(globals().settings.backend);
    }
    return (globals().backendCount > 0) ? globals().backends[0] : nullptr;
}

/** @param[in] id the backend, or NULL for the selected backend. Requires the global lock and loaded settings. */
const MeshBackendApi* resolveBackend(const char* id) {
    return (id != nullptr) ? findBackend(id) : getSelectedBackend();
}

/** Requires the global lock and loaded settings */
MeshBackendSettings& getBackendSettings(const MeshBackendApi* api) {
    auto& entries = globals().backendSettings;
    for (size_t i = 0; i < globals().backendSettingsCount; i++) {
        if (entries[i].api == api) {
            return entries[i].settings;
        }
    }
    if (!globals().legacySettingsMigrated) {
        // The backend that was selected when the settings were saved owns the ones that were stored without a backend
        const MeshBackendApi* owner = getSelectedBackend();
        if (owner != nullptr) {
            mesh_settings_migrate_legacy(owner->id);
            globals().legacySettingsMigrated = true;
        }
    }
    auto& entry = entries[globals().backendSettingsCount < MAX_BACKENDS ? globals().backendSettingsCount++ : MAX_BACKENDS - 1];
    entry.api = api;
    mesh_settings_load_backend(api->id, &entry.settings);
    return entry.settings;
}

size_t getPresetCount(const MeshBackendApi* api) {
    size_t count = 0;
    if (api->presets != nullptr) {
        while (api->presets[count] != nullptr) {
            count++;
        }
    }
    return count;
}

/** @return the index of the preset with @a name, or 0 (the default) when there is none */
size_t findPreset(const MeshBackendApi* api, const char* name) {
    const size_t count = getPresetCount(api);
    for (size_t i = 0; i < count; i++) {
        if (strcmp(api->presets[i], name) == 0) {
            return i;
        }
    }
    return 0;
}

void ensureSettingsLoaded() {
    if (!globals().settingsLoaded) {
        mesh_settings_load(&globals().settings);
        globals().settingsLoaded = true;
    }
}

/** Requires the global lock */
void queueEvent(const MeshEvent& event) {
    for (auto* sub = globals().subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.count >= MESH_EVENT_QUEUE_CAPACITY) {
            continue;
        }
        const uint8_t tail = (sub->internal.head + sub->internal.count) % MESH_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = event;
        sub->internal.count++;
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
}

/** Requires the global lock */
void emitEvent(MeshEventType type, MeshState state, uint32_t seq, MeshDeliveryState delivery) {
    queueEvent({
        .timestamp = get_micros_since_boot(),
        .type = type,
        .state = state,
        .seq = seq,
        .delivery = delivery,
        .frame = {},
    });
}

/** Requires the global lock */
void emitFrame(MeshState state, MeshFrameKind kind, size_t length, float rssi, float snr, const MeshAddress* from, const char* name) {
    MeshEvent event {};
    event.timestamp = get_micros_since_boot();
    event.type = MESH_EVENT_FRAME;
    event.state = state;
    event.delivery = MESH_DELIVERY_RECEIVED;
    event.frame.kind = kind;
    event.frame.length = (uint16_t)length;
    event.frame.rssi = rssi;
    event.frame.snr = snr;
    if (from != nullptr) {
        event.frame.from = *from;
    }
    if (name != nullptr) {
        strncpy(event.frame.name, name, MESH_NAME_MAX_LENGTH);
    }
    queueEvent(event);
}

struct Frame {
    tt::std::vector<uint8_t> data;
    uint32_t seq = 0;
    int busyRetries = 0;
};

struct AirtimeRecord {
    uint64_t timeMs;
    uint32_t airtimeMs;
};

} // namespace

/**
 * The service instance. Fields marked "thread" are only touched by the service thread, the rest is
 * guarded by the global lock.
 */
struct MeshHost {
    MeshState state = MESH_STATE_OFF;
    /** The network parameters of the active backend */
    MeshNetworkSettings network {};
    MeshStore store;
    tt::std::vector<MeshNode> nodes;
    /** The saved contacts of every backend */
    tt::std::vector<MeshContact> contacts;
    char activeBackend[MESH_BACKEND_ID_MAX_LENGTH + 1] = {};
    tt::std::deque<std::function<void()>> calls;

    // thread
    Device* radio = nullptr;
    bool radioAcquired = false;
    TaskEventGroup group {};
    uint32_t stopBit = 0;
    uint32_t callBit = 0;
    RadioRxEventSubscription rxSub {};
    RadioTxEventSubscription txSub {};
    bool rxSubscribed = false;
    bool txSubscribed = false;
    Thread* thread = nullptr;
    std::atomic<bool> running { false };
    const MeshBackendApi* api = nullptr;
    void* backend = nullptr;
    MeshRadioProfile profile {};
    RadioRegion appliedRegion = RADIO_REGION_UNSET;
    tt::std::deque<Frame> txQueue;
    bool txInFlight = false;
    RadioTxId txId = 0;
    Frame txFrame;
    tt::std::deque<AirtimeRecord> airtime;
    RadioRxEvent rxEvent {};
    bool dutyCycleLogged = false;
    uint64_t lastTickMs = 0;

    // Synchronous calls
    EventGroupHandle_t callEvents = nullptr;
};

namespace {

MeshHost* service = nullptr;

/** Requires the global lock */
void setState(MeshHost* host, MeshState state) {
    if (host->state != state) {
        LOG_I(TAG, "State: %s -> %s", mesh_state_to_string(host->state), mesh_state_to_string(state));
        host->state = state;
        emitEvent(MESH_EVENT_STATE, state, 0, MESH_DELIVERY_RECEIVED);
    }
}

void setStateLocked(MeshHost* host, MeshState state) {
    lock();
    setState(host, state);
    unlock();
}

// region Contacts

constexpr size_t MAX_CONTACTS = 64;
constexpr auto* CONTACTS_FILE_NAME = "contacts.bin";

bool isSameAddress(const MeshAddress& a, const MeshAddress& b) {
    return a.length == b.length && memcmp(a.bytes, b.bytes, a.length) == 0;
}

/** Requires the global lock */
MeshContact* findContact(MeshHost* host, const MeshAddress& address) {
    for (auto& contact : host->contacts) {
        if (strncmp(contact.backend, host->activeBackend, MESH_BACKEND_ID_MAX_LENGTH) == 0 && isSameAddress(contact.address, address)) {
            return &contact;
        }
    }
    return nullptr;
}

void loadContacts(MeshHost* host) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_path(MESH_SERVICE_ID, CONTACTS_FILE_NAME, path, sizeof(path)) != ERROR_NONE) {
        return;
    }
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return;
    }
    MeshContact contact;
    while (host->contacts.size() < MAX_CONTACTS && fread(&contact, sizeof(contact), 1, file) == 1) {
        contact.backend[MESH_BACKEND_ID_MAX_LENGTH] = '\0';
        contact.name[MESH_NAME_MAX_LENGTH] = '\0';
        if (contact.address.length > 0 && contact.address.length <= MESH_ADDRESS_MAX_LENGTH) {
            host->contacts.push_back(contact);
        }
    }
    fclose(file);
}

/** Requires the global lock */
void saveContacts(MeshHost* host) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_directory(MESH_SERVICE_ID, directory, sizeof(directory)) != ERROR_NONE ||
        service_paths_get_user_data_path(MESH_SERVICE_ID, CONTACTS_FILE_NAME, path, sizeof(path)) != ERROR_NONE) {
        return;
    }
    const error_t made = directory_make(directory, true);
    if (made != ERROR_NONE && made != ERROR_ALREADY_EXISTS) {
        LOG_E(TAG, "Failed to create %s", directory);
        return;
    }
    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        LOG_E(TAG, "Failed to save the contacts");
        return;
    }
    if (!host->contacts.empty() && fwrite(host->contacts.data(), sizeof(MeshContact), host->contacts.size(), file) != host->contacts.size()) {
        LOG_E(TAG, "Failed to save the contacts");
    }
    fclose(file);
}

/**
 * Save a contact of the active backend. An existing contact is renamed when @a rename is set.
 * Requires the global lock.
 */
error_t addContact(MeshHost* host, const MeshAddress& address, const char* name, bool rename) {
    if (address.length == 0 || address.length > MESH_ADDRESS_MAX_LENGTH) {
        return ERROR_INVALID_ARGUMENT;
    }

    char resolvedName[MESH_NAME_MAX_LENGTH + 1] = {};
    if (name != nullptr) {
        strncpy(resolvedName, name, MESH_NAME_MAX_LENGTH);
    }
    if (resolvedName[0] == '\0') {
        for (const auto& node : host->nodes) {
            if (isSameAddress(node.address, address) && strncmp(node.backend, host->activeBackend, MESH_BACKEND_ID_MAX_LENGTH) == 0) {
                strncpy(resolvedName, node.name, MESH_NAME_MAX_LENGTH);
                break;
            }
        }
    }
    if (resolvedName[0] == '\0') {
        // The first bytes identify the node well enough to tell nodes apart
        for (size_t i = 0; i < address.length && i < 4; i++) {
            snprintf(resolvedName + i * 2, 3, "%02x", address.bytes[i]);
        }
    }

    auto* existing = findContact(host, address);
    if (existing != nullptr) {
        if (!rename || name == nullptr || name[0] == '\0') {
            return ERROR_NONE;
        }
        strncpy(existing->name, resolvedName, MESH_NAME_MAX_LENGTH);
    } else {
        if (host->contacts.size() >= MAX_CONTACTS) {
            return ERROR_RESOURCE;
        }
        MeshContact contact {};
        strncpy(contact.backend, host->activeBackend, MESH_BACKEND_ID_MAX_LENGTH);
        contact.address = address;
        strncpy(contact.name, resolvedName, MESH_NAME_MAX_LENGTH);
        host->contacts.push_back(contact);
    }
    saveContacts(host);
    emitEvent(MESH_EVENT_CONTACT, host->state, 0, MESH_DELIVERY_RECEIVED);
    return ERROR_NONE;
}

// endregion

/** Requires the global lock */
void setDelivery(MeshHost* host, uint32_t seq, MeshDeliveryState delivery) {
    if (host->store.setDelivery(seq, delivery) == ERROR_NONE) {
        emitEvent(MESH_EVENT_DELIVERY, host->state, seq, delivery);
    }
}

/** Queue work for the service thread. Requires the global lock. */
void post(MeshHost* host, std::function<void()> work) {
    host->calls.push_back(std::move(work));
    task_event_group_signal(&host->group, host->callBit);
}

/**
 * Run work on the service thread and wait for it. Must be called without the global lock and never from
 * the service thread. The thread drains all posted work before it exits, so the wait always ends.
 */
error_t callOnServiceThread(const std::function<void(MeshHost*)>& work) {
    mutex_lock(&callMutex.handle);
    lock();
    MeshHost* host = service;
    if (host == nullptr) {
        unlock();
        mutex_unlock(&callMutex.handle);
        return ERROR_INVALID_STATE;
    }
    event_group_clear(host->callEvents, CALL_DONE_BIT);
    post(host, [host, &work] {
        work(host);
        event_group_set(host->callEvents, CALL_DONE_BIT);
    });
    unlock();
    event_group_wait(host->callEvents, CALL_DONE_BIT, false, true, nullptr, portMAX_DELAY);
    mutex_unlock(&callMutex.handle);
    return ERROR_NONE;
}

// region Radio and backend lifecycle (service thread)

/** Subscribes to the events of @a radio, which is a referenced device. */
void attachRadio(MeshHost* host, Device* radio) {
    host->radio = radio;
    host->rxSubscribed = radio_rx_event_subscribe(&host->rxSub, &host->group, host->radio) == ERROR_NONE;
    host->txSubscribed = radio_tx_event_subscribe(&host->txSub, &host->group, host->radio) == ERROR_NONE;
}

/** Unsubscribes from the radio, releases it and drops the reference */
void detachRadio(MeshHost* host) {
    if (host->rxSubscribed) {
        radio_rx_event_unsubscribe(&host->rxSub);
        host->rxSubscribed = false;
    }
    if (host->txSubscribed) {
        radio_tx_event_unsubscribe(&host->txSub);
        host->txSubscribed = false;
    }
    if (host->radio != nullptr) {
        if (host->radioAcquired) {
            radio_release(host->radio);
            host->radioAcquired = false;
        }
        device_put(host->radio);
        host->radio = nullptr;
    }
}

void radioOff(MeshHost* host) {
    if (host->radio != nullptr && host->radioAcquired) {
        radio_set_enabled(host->radio, false);
    }
}

void deactivateBackend(MeshHost* host) {
    if (host->api == nullptr) {
        return;
    }

    const char* id = host->api->id;
    host->api->destroy(host->backend);
    host->api = nullptr;
    host->backend = nullptr;
    host->txQueue.clear();
    host->txInFlight = false;
    radioOff(host);

    lock();
    // Messages that weren't sent wait for this backend to come back
    for (uint32_t seq : host->store.findOutgoing(id, MESH_DELIVERY_QUEUED)) {
        setDelivery(host, seq, MESH_DELIVERY_BLOCKED);
    }
    host->nodes.clear();
    host->activeBackend[0] = '\0';
    unlock();
}

bool applyProfile(MeshHost* host) {
    const auto& profile = host->profile;
    radio_set_enabled(host->radio, false);
    if (radio_set_modulation(host->radio, profile.modulation) != ERROR_NONE) {
        LOG_E(TAG, "Radio doesn't support the backend's modulation");
        return false;
    }

    const struct {
        RadioParameter parameter;
        int32_t value;
    } parameters[] = {
        { RADIO_PARAMETER_FREQUENCY, profile.frequency },
        { RADIO_PARAMETER_BANDWIDTH, profile.bandwidth },
        { RADIO_PARAMETER_SPREADING_FACTOR, profile.spreading_factor },
        { RADIO_PARAMETER_CODING_RATE, profile.coding_rate },
        { RADIO_PARAMETER_SYNC_WORD, profile.sync_word },
        { RADIO_PARAMETER_PREAMBLE_LENGTH, profile.preamble_length },
        { RADIO_PARAMETER_POWER, profile.power },
    };
    for (const auto& entry : parameters) {
        const error_t result = radio_set_parameter(host->radio, entry.parameter, entry.value);
        if (result != ERROR_NONE && result != ERROR_NOT_SUPPORTED) {
            LOG_E(TAG, "Setting radio parameter %d to %d failed: %d", (int)entry.parameter, (int)entry.value, (int)result);
            return false;
        }
    }
    if (profile.channel != 0) {
        const error_t result = radio_set_parameter(host->radio, RADIO_PARAMETER_CHANNEL, profile.channel);
        if (result != ERROR_NONE) {
            LOG_E(TAG, "Setting the radio channel to %d failed: %d", (int)profile.channel, (int)result);
            return false;
        }
    }
    // Listen-before-talk is optional, so a driver without it is fine
    radio_set_parameter(host->radio, RADIO_PARAMETER_LBT, 1);

    if (radio_set_enabled(host->radio, true) != ERROR_NONE) {
        LOG_E(TAG, "Failed to turn on the radio");
        return false;
    }
    return true;
}

void resubmitPending(MeshHost* host);

/**
 * The profile of a preset with the settings applied, and the power limited to what the region allows (the radio rejects more).
 * @param[in] backend the backend state, or NULL to only get the parameters of the preset
 */
error_t getRadioProfile(const MeshBackendApi* api, void* backend, RadioRegion region, RadioModulation modulation, size_t preset, const MeshRadioSettings& settings, MeshRadioProfile* profile) {
    const error_t error = api->get_radio_profile(backend, region, modulation, preset, profile);
    if (error != ERROR_NONE || modulation != RADIO_MODULATION_LORA) {
        return error;
    }
    if (settings.frequency != 0) profile->frequency = settings.frequency;
    if (settings.bandwidth != 0) profile->bandwidth = settings.bandwidth;
    if (settings.spreading_factor != 0) profile->spreading_factor = settings.spreading_factor;
    if (settings.coding_rate != 0) profile->coding_rate = settings.coding_rate;
    if (settings.power != 0) profile->power = settings.power;

    const RadioRegionBand* bands = nullptr;
    const size_t bandCount = radio_get_region_bands(region, &bands);
    int32_t maxPower = 0;
    for (size_t i = 0; i < bandCount; i++) {
        maxPower = std::max<int32_t>(maxPower, bands[i].power_max);
    }
    if (bandCount > 0) {
        profile->power = std::min(profile->power, maxPower);
    }
    return ERROR_NONE;
}

/** The preset's network parameters with the settings applied */
MeshNetworkSettings getNetwork(const MeshBackendApi* api, size_t preset, const MeshNetworkSettings& settings) {
    MeshNetworkSettings network = {};
    if (api->get_preset_network != nullptr) {
        api->get_preset_network(preset, &network);
    }
    if (settings.path_hash_size != 0) {
        network.path_hash_size = settings.path_hash_size;
    }
    if (settings.region_scope[0] == '\0') {
        // The preset's scope
    } else if (strcmp(settings.region_scope, MESH_REGION_SCOPE_NONE) == 0) {
        network.region_scope[0] = '\0';
    } else {
        strncpy(network.region_scope, settings.region_scope, MESH_NAME_MAX_LENGTH);
    }
    return network;
}

/** (Re)select the backend and configure the radio from the settings and the radio's region */
void configure(MeshHost* host) {
    deactivateBackend(host);

    if (host->radio == nullptr) {
        setStateLocked(host, MESH_STATE_NO_RADIO);
        return;
    }
    if (!host->radioAcquired) {
        host->radioAcquired = radio_acquire(host->radio, "mesh") == ERROR_NONE;
        if (!host->radioAcquired) {
            setStateLocked(host, MESH_STATE_RADIO_BUSY);
            return;
        }
    }

    lock();
    const MeshBackendApi* api = getSelectedBackend();
    const MeshBackendSettings backendSettings = (api != nullptr) ? getBackendSettings(api) : MeshBackendSettings {};
    const size_t preset = (api != nullptr) ? findPreset(api, backendSettings.preset) : 0;
    unlock();
    if (api == nullptr) {
        setStateLocked(host, MESH_STATE_NO_BACKEND);
        return;
    }

    // Only LoRa is bound to a region. The Wi-Fi stack limits ESP-NOW by country.
    host->appliedRegion = radio_get_region(host->radio);
    if (host->appliedRegion == RADIO_REGION_UNSET && usesLora(host->radio)) {
        setStateLocked(host, MESH_STATE_NO_REGION);
        return;
    }

    // Set before create() because backends use host functions there
    host->api = api;
    void* backend = api->create(host);
    if (backend == nullptr) {
        host->api = nullptr;
        setStateLocked(host, MESH_STATE_ERROR);
        return;
    }
    // A radio that doesn't do LoRa does ESP-NOW
    const RadioModulation modulation = usesLora(host->radio) ? RADIO_MODULATION_LORA : RADIO_MODULATION_ESPNOW;
    if (getRadioProfile(api, backend, host->appliedRegion, modulation, preset, backendSettings.radio_settings, &host->profile) != ERROR_NONE) {
        LOG_E(TAG, "Backend %s doesn't support region %s with preset %u", api->id, radio_region_to_string(host->appliedRegion), (unsigned)preset);
        api->destroy(backend);
        host->api = nullptr;
        setStateLocked(host, MESH_STATE_ERROR);
        return;
    }
    lock();
    host->network = getNetwork(api, preset, backendSettings.network);
    unlock();
    host->backend = backend;

    if (!applyProfile(host)) {
        deactivateBackend(host);
        setStateLocked(host, MESH_STATE_ERROR);
        return;
    }

    lock();
    strncpy(host->activeBackend, api->id, MESH_BACKEND_ID_MAX_LENGTH);
    host->activeBackend[MESH_BACKEND_ID_MAX_LENGTH] = '\0';
    setState(host, MESH_STATE_ON);
    unlock();

    LOG_I(TAG, "Backend %s active in region %s", api->id, radio_region_to_string(host->appliedRegion));
    api->on_start(backend);
    resubmitPending(host);
}

void sendStored(MeshHost* host, uint32_t seq) {
    MeshMessage message;
    lock();
    const bool found = host->store.get(seq, &message) == ERROR_NONE;
    unlock();
    if (!found || message.delivery != MESH_DELIVERY_QUEUED) {
        return;
    }
    if (host->api == nullptr || strcmp(message.backend, host->api->id) != 0) {
        lock();
        setDelivery(host, seq, MESH_DELIVERY_BLOCKED);
        unlock();
        return;
    }

    const MeshAddress* to = (message.peer.length > 0) ? &message.peer : nullptr;
    if (host->api->send_text(host->backend, seq, to, message.channel, message.text) != ERROR_NONE) {
        lock();
        setDelivery(host, seq, MESH_DELIVERY_FAILED);
        unlock();
    }
}

void resubmitPending(MeshHost* host) {
    lock();
    tt::std::vector<uint32_t> pending = host->store.findOutgoing(host->api->id, MESH_DELIVERY_BLOCKED);
    for (uint32_t seq : pending) {
        setDelivery(host, seq, MESH_DELIVERY_QUEUED);
    }
    unlock();
    for (uint32_t seq : pending) {
        sendStored(host, seq);
    }
}

// endregion

// region TX (service thread)

uint32_t getAirtimeUsedMs(MeshHost* host, uint64_t nowMs) {
    while (!host->airtime.empty() && nowMs - host->airtime.front().timeMs > DUTY_CYCLE_WINDOW_MS) {
        host->airtime.pop_front();
    }
    uint32_t used = 0;
    for (const auto& record : host->airtime) {
        used += record.airtimeMs;
    }
    return used;
}

void pumpTx(MeshHost* host) {
    if (host->txInFlight || host->txQueue.empty() || host->api == nullptr) {
        return;
    }

    const auto& frame = host->txQueue.front();
    const uint32_t airtimeMs = mesh_airtime_estimate_ms(host->profile, frame.data.size());
    // Wi-Fi radios have no duty cycle limit
    const uint32_t dutyCycle = (host->profile.modulation == RADIO_MODULATION_ESPNOW) ? 100 : mesh_airtime_duty_cycle_percent(host->appliedRegion, host->profile.frequency);
    const uint64_t nowMs = getMillis();
    if (dutyCycle < 100) {
        const uint64_t budgetMs = DUTY_CYCLE_WINDOW_MS * dutyCycle / 100;
        if (getAirtimeUsedMs(host, nowMs) + airtimeMs > budgetMs) {
            if (!host->dutyCycleLogged) {
                LOG_W(TAG, "Duty cycle budget used up, holding %u frames", (unsigned)host->txQueue.size());
                host->dutyCycleLogged = true;
            }
            return;
        }
    }
    host->dutyCycleLogged = false;

    host->txFrame = std::move(host->txQueue.front());
    host->txQueue.pop_front();
    if (radio_transmit(host->radio, host->txFrame.data.data(), host->txFrame.data.size(), &host->txId) != ERROR_NONE) {
        if (host->txFrame.seq != 0) {
            lock();
            setDelivery(host, host->txFrame.seq, MESH_DELIVERY_FAILED);
            unlock();
        }
        return;
    }
    host->txInFlight = true;
    host->airtime.push_back({ .timeMs = nowMs, .airtimeMs = airtimeMs });
}

void onTxEvent(MeshHost* host, const RadioTxEvent& event) {
    if (!host->txInFlight || event.id != host->txId) {
        return;
    }

    const uint32_t seq = host->txFrame.seq;
    switch (event.state) {
        case RADIO_TRANSMISSION_STATE_TRANSMITTED:
            host->txInFlight = false;
            if (seq != 0) {
                lock();
                MeshMessage message;
                if (host->store.get(seq, &message) == ERROR_NONE && message.delivery == MESH_DELIVERY_QUEUED) {
                    setDelivery(host, seq, MESH_DELIVERY_SENT);
                }
                unlock();
            }
            break;
        case RADIO_TRANSMISSION_STATE_CHANNEL_BUSY:
            host->txInFlight = false;
            if (host->txFrame.busyRetries < CHANNEL_BUSY_RETRIES) {
                host->txFrame.busyRetries++;
                host->txQueue.push_front(std::move(host->txFrame));
                break;
            }
            [[fallthrough]];
        case RADIO_TRANSMISSION_STATE_TIMEOUT:
        case RADIO_TRANSMISSION_STATE_ERROR:
            host->txInFlight = false;
            if (seq != 0) {
                lock();
                setDelivery(host, seq, MESH_DELIVERY_FAILED);
                unlock();
            }
            break;
        default:
            break;
    }
}

// endregion

// region Service thread

/** Takes the first radio and subscribes to its events. @return false when there is none */
bool attachRadio(MeshHost* host) {
    Device* radio = findRadio();
    if (radio == nullptr) {
        return false;
    }
    attachRadio(host, radio);
    return true;
}

/** Moves to the radio that is selected in the settings */
void reselectRadio(MeshHost* host) {
    deactivateBackend(host);
    detachRadio(host);
    if (!attachRadio(host)) {
        setStateLocked(host, MESH_STATE_NO_RADIO);
        return;
    }
    configure(host);
}

void drainRadioEvents(MeshHost* host) {
    if (host->rxSubscribed) {
        // A host member: RadioRxEvent is large and only used on this thread
        auto& rxEvent = host->rxEvent;
        while (radio_rx_event_poll(&host->rxSub, &rxEvent) == ERROR_NONE) {
            if (host->api != nullptr) {
                if (!host->api->on_receive(host->backend, rxEvent.data, rxEvent.length, rxEvent.rssi, rxEvent.snr)) {
                    lock();
                    emitFrame(host->state, MESH_FRAME_UNREADABLE, rxEvent.length, rxEvent.rssi, rxEvent.snr, nullptr, nullptr);
                    unlock();
                }
            }
        }
    }
    if (host->txSubscribed) {
        RadioTxEvent txEvent;
        while (radio_tx_event_poll(&host->txSub, &txEvent) == ERROR_NONE) {
            onTxEvent(host, txEvent);
        }
    }
}

void runCalls(MeshHost* host) {
    while (true) {
        lock();
        if (host->calls.empty()) {
            unlock();
            return;
        }
        auto work = std::move(host->calls.front());
        host->calls.pop_front();
        unlock();
        work();
    }
}

void tick(MeshHost* host) {
    // Radios can start after the service, e.g. once the hotplug poller detects an add-on
    if (host->radio == nullptr && attachRadio(host)) {
        LOG_I(TAG, "Mesh radio found");
        configure(host);
    }
    if (host->radio != nullptr && host->radioAcquired && radio_get_region(host->radio) != host->appliedRegion) {
        LOG_I(TAG, "Region changed, reconfiguring");
        configure(host);
    }
    RadioState radioState = RADIO_STATE_OFF;
    if (host->api != nullptr && radio_get_state(host->radio, &radioState) == ERROR_NONE && radioState == RADIO_STATE_ERROR) {
        LOG_E(TAG, "Radio failed");
        deactivateBackend(host);
        setStateLocked(host, MESH_STATE_ERROR);
    }
    if (host->api != nullptr) {
        host->api->on_tick(host->backend);
    }
    lock();
    host->store.flushIfNeeded(getMillis(), FLUSH_MAX_AGE_MS);
    unlock();
}

int32_t threadMain(void* context) {
    auto* host = static_cast<MeshHost*>(context);
    configure(host);

    while (host->running) {
        uint32_t flags = 0;
        task_event_group_wait_any(&host->group, &flags, pdMS_TO_TICKS(TICK_INTERVAL_MS));
        if (!host->running) {
            break;
        }

        drainRadioEvents(host);
        runCalls(host);

        const uint64_t nowMs = getMillis();
        if (nowMs - host->lastTickMs >= TICK_INTERVAL_MS) {
            host->lastTickMs = nowMs;
            tick(host);
        }
        pumpTx(host);
    }

    runCalls(host);
    deactivateBackend(host);
    return 0;
}

// endregion

// region Service lifecycle

void* createService(const ServiceManifest*) {
    return tt::newObject<MeshHost>();
}

void destroyService(const ServiceManifest*, void* data) {
    tt::deleteObject(static_cast<MeshHost*>(data));
}

void releaseResources(MeshHost* host) {
    detachRadio(host);
    task_event_group_destruct(&host->group);
    event_group_destruct(&host->callEvents);
}

error_t onStart(ServiceInstance*, void* data) {
    auto* host = static_cast<MeshHost*>(data);

    event_group_construct(&host->callEvents);
    task_event_group_construct(&host->group);
    if (task_event_group_claim_bit(&host->group, &host->stopBit) != ERROR_NONE ||
        task_event_group_claim_bit(&host->group, &host->callBit) != ERROR_NONE) {
        releaseResources(host);
        return ERROR_RESOURCE;
    }

    char storePath[FILE_MAX_PATH_STRING_LENGTH];
    if (service_paths_get_user_data_path(MESH_SERVICE_ID, "store", storePath, sizeof(storePath)) != ERROR_NONE ||
        host->store.open(storePath) != ERROR_NONE) {
        LOG_E(TAG, "Failed to open the message store");
        releaseResources(host);
        return ERROR_RESOURCE;
    }
    loadContacts(host);

    if (!attachRadio(host)) {
        LOG_W(TAG, "No mesh radio found yet");
        device_for_each_of_type(&RADIO_TYPE, nullptr, [](Device* device, void*) {
            LOG_I(TAG, "Radio %s: ready=%d lora=%d espnow=%d", device->name, (int)device_is_ready(device),
                (int)usesLora(device), (int)canSendAndReceive(device, RADIO_MODULATION_ESPNOW));
            return true;
        });
    }

    lock();
    ensureSettingsLoaded();
    service = host;
    unlock();

    host->running = true;
    host->thread = thread_alloc_full("mesh", THREAD_STACK_SIZE, threadMain, host, -1);
    if (host->thread == nullptr || thread_start(host->thread) != ERROR_NONE) {
        host->running = false;
        if (host->thread != nullptr) {
            thread_free(host->thread);
            host->thread = nullptr;
        }
        lock();
        service = nullptr;
        unlock();
        host->store.close();
        releaseResources(host);
        return ERROR_RESOURCE;
    }
    return ERROR_NONE;
}

void onStop(ServiceInstance*, void* data) {
    auto* host = static_cast<MeshHost*>(data);

    // No new work can be posted from here on, and the thread drains what was posted before
    lock();
    service = nullptr;
    unlock();

    host->running = false;
    task_event_group_signal(&host->group, host->stopBit);
    thread_join(host->thread, portMAX_DELAY, pdMS_TO_TICKS(10));
    thread_free(host->thread);
    host->thread = nullptr;

    lock();
    host->store.close();
    setState(host, MESH_STATE_OFF);
    unlock();

    releaseResources(host);
}

// endregion

} // namespace

const ServiceManifest mesh_service_manifest = {
    .id = MESH_SERVICE_ID,
    .create_service = createService,
    .destroy_service = destroyService,
    .on_start = onStart,
    .on_stop = onStop,
};

bool mesh_service_should_auto_start() {
    lock();
    ensureSettingsLoaded();
    const bool enabled = globals().settings.enabled;
    unlock();
    return enabled;
}

extern "C" {

// region Backend registry

error_t mesh_backend_register(const MeshBackendApi* api) {
    lock();
    error_t result = ERROR_NONE;
    if (findBackend(api->id) != nullptr) {
        result = ERROR_ALREADY_EXISTS;
    } else if (globals().backendCount >= MAX_BACKENDS) {
        result = ERROR_RESOURCE;
    } else {
        globals().backends[globals().backendCount++] = api;
        if (service != nullptr && service->state == MESH_STATE_NO_BACKEND) {
            MeshHost* host = service;
            post(host, [host] { configure(host); });
        }
    }
    unlock();
    return result;
}

error_t mesh_backend_unregister(const MeshBackendApi* api) {
    lock();
    error_t result = ERROR_NOT_FOUND;
    for (size_t i = 0; i < globals().backendCount; i++) {
        if (globals().backends[i] == api) {
            globals().backends[i] = globals().backends[--globals().backendCount];
            globals().backends[globals().backendCount] = nullptr;
            result = ERROR_NONE;
            break;
        }
    }
    MeshHost* host = service;
    unlock();

    // The service may be using it, so wait for the service thread to let go of it
    if (result == ERROR_NONE && host != nullptr) {
        callOnServiceThread([api](MeshHost* serviceHost) {
            if (serviceHost->api == api) {
                configure(serviceHost);
            }
        });
    }
    return result;
}

// endregion

// region Host functions

error_t mesh_host_transmit(MeshHost* host, const uint8_t* data, size_t length, uint32_t seq) {
    if (length == 0 || length > RADIO_RX_MAX_PACKET_LENGTH) {
        return ERROR_OUT_OF_RANGE;
    }
    if (host->txQueue.size() >= TX_QUEUE_LIMIT) {
        return ERROR_RESOURCE;
    }
    host->txQueue.push_back(Frame { .data = tt::std::vector<uint8_t>(data, data + length), .seq = seq, .busyRetries = 0 });
    return ERROR_NONE;
}

uint32_t mesh_host_receive_text(
    MeshHost* host,
    const MeshAddress* from,
    uint8_t channel,
    const char* sender_name,
    const char* text,
    float rssi,
    float snr,
    int64_t sender_time
) {
    MeshMessage message {};
    strncpy(message.backend, host->api->id, MESH_BACKEND_ID_MAX_LENGTH);
    message.outgoing = false;
    message.delivery = MESH_DELIVERY_RECEIVED;
    if (from != nullptr) {
        message.peer = *from;
    }
    message.channel = channel;
    if (sender_name != nullptr) {
        strncpy(message.sender_name, sender_name, MESH_NAME_MAX_LENGTH);
    }
    const int64_t now = (int64_t)time(nullptr);
    if (now >= MIN_VALID_WALL_TIME) {
        message.wall_time = now;
        message.time_source = MESH_TIME_SOURCE_SYSTEM;
    } else if (sender_time >= MIN_VALID_WALL_TIME) {
        message.wall_time = sender_time;
        message.time_source = MESH_TIME_SOURCE_MESH;
    } else {
        message.time_source = MESH_TIME_SOURCE_NONE;
    }
    message.uptime_ms = getMillis();
    message.rssi = rssi;
    message.snr = snr;
    strncpy(message.text, text, MESH_TEXT_MAX_LENGTH);

    lock();
    uint32_t seq = 0;
    if (host->store.add(&message) == ERROR_NONE) {
        seq = message.seq;
        // A sender of a direct message is saved, so the conversation can be found and answered
        if (from != nullptr && from->length > 0) {
            addContact(host, *from, sender_name, false);
        }
        emitEvent(MESH_EVENT_MESSAGE, host->state, seq, MESH_DELIVERY_RECEIVED);
    }
    unlock();
    return seq;
}

void mesh_host_set_delivery(MeshHost* host, uint32_t seq, MeshDeliveryState delivery) {
    lock();
    setDelivery(host, seq, delivery);
    unlock();
}

void mesh_host_node_heard(MeshHost* host, const MeshAddress* address, const char* name, float rssi, float snr) {
    lock();
    MeshNode* node = nullptr;
    for (auto& candidate : host->nodes) {
        if (candidate.address.length == address->length && memcmp(candidate.address.bytes, address->bytes, address->length) == 0) {
            node = &candidate;
            break;
        }
    }
    if (node == nullptr) {
        if (host->nodes.size() >= MAX_NODES) {
            // Replace the node that wasn't heard for the longest time
            auto oldest = host->nodes.begin();
            for (auto iterator = host->nodes.begin(); iterator != host->nodes.end(); ++iterator) {
                if (iterator->last_heard_ms < oldest->last_heard_ms) {
                    oldest = iterator;
                }
            }
            host->nodes.erase(oldest);
        }
        host->nodes.push_back({});
        node = &host->nodes.back();
        strncpy(node->backend, host->activeBackend, MESH_BACKEND_ID_MAX_LENGTH);
        node->address = *address;
    }
    if (name != nullptr) {
        strncpy(node->name, name, MESH_NAME_MAX_LENGTH);
        node->name[MESH_NAME_MAX_LENGTH] = '\0';
    }
    node->last_heard_ms = getMillis();
    node->rssi = rssi;
    node->snr = snr;
    emitEvent(MESH_EVENT_NODE, host->state, 0, MESH_DELIVERY_RECEIVED);
    unlock();
}

void mesh_host_advert_heard(MeshHost* host, const MeshAddress* address, const char* name, float rssi, float snr) {
    mesh_host_node_heard(host, address, name, rssi, snr);
    lock();
    emitFrame(host->state, MESH_FRAME_ADVERT, 0, rssi, snr, address, name);
    unlock();
}

error_t mesh_host_get_data_path(MeshHost* host, const char* child_path, char* out_path, size_t out_path_size) {
    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char backendDirectory[16 + MESH_BACKEND_ID_MAX_LENGTH];
    snprintf(backendDirectory, sizeof(backendDirectory), "backends/%s", host->api->id);
    if (service_paths_get_user_data_path(MESH_SERVICE_ID, backendDirectory, directory, sizeof(directory)) != ERROR_NONE) {
        return ERROR_BUFFER_OVERFLOW;
    }
    const error_t made = directory_make(directory, true);
    if (made != ERROR_NONE && made != ERROR_ALREADY_EXISTS) {
        return ERROR_RESOURCE;
    }
    const int written = snprintf(out_path, out_path_size, "%s/%s", directory, child_path);
    return (written < 0 || (size_t)written >= out_path_size) ? ERROR_BUFFER_OVERFLOW : ERROR_NONE;
}

void mesh_host_get_network(MeshHost* host, MeshNetworkSettings* network) {
    lock();
    *network = host->network;
    unlock();
}

void mesh_host_get_node_name(MeshHost*, char* name, size_t name_size) {
    lock();
    ensureSettingsLoaded();
    strncpy(name, globals().settings.node_name, name_size - 1);
    name[name_size - 1] = '\0';
    unlock();
}

// endregion

// region Public API

const char* mesh_state_to_string(MeshState state) {
    switch (state) {
        case MESH_STATE_OFF:
            return "off";
        case MESH_STATE_ON:
            return "on";
        case MESH_STATE_NO_RADIO:
            return "no radio";
        case MESH_STATE_RADIO_BUSY:
            return "radio busy";
        case MESH_STATE_NO_REGION:
            return "no region";
        case MESH_STATE_NO_BACKEND:
            return "no backend";
        case MESH_STATE_ERROR:
            return "error";
        default:
            return "unknown";
    }
}

error_t mesh_set_enabled(bool enabled) {
    lock();
    ensureSettingsLoaded();
    globals().settings.enabled = enabled;
    const MeshSettings copy = globals().settings;
    unlock();

    const error_t saved = mesh_settings_save(&copy);
    const error_t result = enabled ? service_manager_start(MESH_SERVICE_ID) : service_manager_stop(MESH_SERVICE_ID);
    return (result != ERROR_NONE) ? result : saved;
}

bool mesh_is_enabled(void) {
    lock();
    ensureSettingsLoaded();
    const bool enabled = globals().settings.enabled;
    unlock();
    return enabled;
}

MeshState mesh_get_state(void) {
    lock();
    const MeshState state = (service != nullptr) ? service->state : MESH_STATE_OFF;
    unlock();
    return state;
}

error_t mesh_set_node_name(const char* name) {
    if (name == nullptr || name[0] == '\0' || strlen(name) > MESH_NAME_MAX_LENGTH) {
        return ERROR_INVALID_ARGUMENT;
    }
    lock();
    ensureSettingsLoaded();
    strncpy(globals().settings.node_name, name, MESH_NAME_MAX_LENGTH);
    globals().settings.node_name[MESH_NAME_MAX_LENGTH] = '\0';
    const MeshSettings copy = globals().settings;
    // The backend announces the new name
    if (service != nullptr) {
        post(service, [host = service] {
            if (host->api != nullptr && host->api->on_node_name_changed != nullptr) {
                host->api->on_node_name_changed(host->backend);
            }
        });
    }
    unlock();
    return mesh_settings_save(&copy);
}

error_t mesh_get_node_name(char* name, size_t name_size) {
    if (name_size == 0) {
        return ERROR_BUFFER_OVERFLOW;
    }
    mesh_host_get_node_name(nullptr, name, name_size);
    return ERROR_NONE;
}

bool mesh_is_available(void) {
    Device* radio = findRadio();
    if (radio == nullptr) {
        return false;
    }
    device_put(radio);
    return true;
}

size_t mesh_get_radio_count(void) {
    size_t count = 0;
    device_for_each_of_type(&RADIO_TYPE, &count, [](Device* device, void* context) {
        if (isMeshRadio(device)) {
            (*static_cast<size_t*>(context))++;
        }
        return true;
    });
    return count;
}

error_t mesh_get_radio_kind(size_t index, MeshRadioKind* kind) {
    struct Search {
        size_t index;
        MeshRadioKind* kind;
        error_t result;
    } search = { index, kind, ERROR_NOT_FOUND };
    device_for_each_of_type(&RADIO_TYPE, &search, [](Device* device, void* context) {
        auto* search = static_cast<Search*>(context);
        if (!isMeshRadio(device)) {
            return true;
        }
        if (search->index-- > 0) {
            return true;
        }
        *search->kind = usesLora(device) ? MESH_RADIO_LORA : MESH_RADIO_ESPNOW;
        search->result = ERROR_NONE;
        return false;
    });
    return search.result;
}

error_t mesh_get_radio_name(size_t index, char* name, size_t name_size) {
    struct Search {
        size_t index;
        char* name;
        size_t size;
        error_t result;
    } search = { index, name, name_size, ERROR_NOT_FOUND };
    device_for_each_of_type(&RADIO_TYPE, &search, [](Device* device, void* context) {
        auto* search = static_cast<Search*>(context);
        if (!isMeshRadio(device)) {
            return true;
        }
        if (search->index-- > 0) {
            return true;
        }
        if (search->size > 0) {
            strncpy(search->name, device->name, search->size - 1);
            search->name[search->size - 1] = '\0';
            search->result = ERROR_NONE;
        }
        return false;
    });
    return search.result;
}

error_t mesh_get_radio(char* name, size_t name_size) {
    Device* radio = findRadio();
    if (radio == nullptr || name_size == 0) {
        if (radio != nullptr) {
            device_put(radio);
        }
        return ERROR_NOT_FOUND;
    }
    strncpy(name, radio->name, name_size - 1);
    name[name_size - 1] = '\0';
    device_put(radio);
    return ERROR_NONE;
}

error_t mesh_set_radio(const char* name) {
    const char* selected = (name != nullptr) ? name : "";
    if (selected[0] != '\0') {
        bool found = false;
        for (size_t i = 0, count = mesh_get_radio_count(); i < count && !found; i++) {
            char radioName[MESH_NAME_MAX_LENGTH + 1];
            found = mesh_get_radio_name(i, radioName, sizeof(radioName)) == ERROR_NONE && strcmp(radioName, selected) == 0;
        }
        if (!found) {
            return ERROR_NOT_FOUND;
        }
    }

    lock();
    ensureSettingsLoaded();
    strncpy(globals().settings.radio, selected, MESH_NAME_MAX_LENGTH);
    globals().settings.radio[MESH_NAME_MAX_LENGTH] = '\0';
    const MeshSettings copy = globals().settings;
    if (service != nullptr) {
        MeshHost* host = service;
        post(host, [host] { reselectRadio(host); });
    }
    unlock();
    return mesh_settings_save(&copy);
}

size_t mesh_get_backend_count(void) {
    lock();
    const size_t count = globals().backendCount;
    unlock();
    return count;
}

error_t mesh_get_backend_id(size_t index, char* id, size_t id_size) {
    lock();
    error_t result = ERROR_NOT_FOUND;
    if (index < globals().backendCount && id_size > 0) {
        strncpy(id, globals().backends[index]->id, id_size - 1);
        id[id_size - 1] = '\0';
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_set_backend(const char* id) {
    lock();
    if (findBackend(id) == nullptr) {
        unlock();
        return ERROR_NOT_FOUND;
    }
    ensureSettingsLoaded();
    strncpy(globals().settings.backend, id, MESH_BACKEND_ID_MAX_LENGTH);
    globals().settings.backend[MESH_BACKEND_ID_MAX_LENGTH] = '\0';
    const MeshSettings copy = globals().settings;
    if (service != nullptr) {
        MeshHost* host = service;
        post(host, [host] { configure(host); });
    }
    unlock();
    return mesh_settings_save(&copy);
}

error_t mesh_get_backend(char* id, size_t id_size) {
    lock();
    ensureSettingsLoaded();
    const char* selected = globals().settings.backend;
    if (service != nullptr && service->activeBackend[0] != '\0') {
        selected = service->activeBackend;
    }
    error_t result = ERROR_NOT_FOUND;
    if (selected[0] != '\0' && id_size > 0) {
        strncpy(id, selected, id_size - 1);
        id[id_size - 1] = '\0';
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

size_t mesh_get_preset_count(const char* backend) {
    const bool applies = presetsApply();
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    const size_t count = (api != nullptr && applies) ? getPresetCount(api) : 0;
    unlock();
    return count;
}

error_t mesh_get_preset_name(const char* backend, size_t index, char* name, size_t name_size) {
    const bool applies = presetsApply();
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    error_t result = ERROR_NOT_FOUND;
    if (api != nullptr && applies && index < getPresetCount(api) && name_size > 0) {
        strncpy(name, api->presets[index], name_size - 1);
        name[name_size - 1] = '\0';
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

/**
 * Persists the settings of a backend, and only when that worked uses them: the cache is updated and the radio is
 * reconfigured when the backend is selected. Called without the global lock, because saving does file operations.
 */
static error_t commitBackendSettings(const MeshBackendApi* api, const MeshBackendSettings& settings) {
    const error_t saved = mesh_settings_save_backend(api->id, &settings);
    if (saved != ERROR_NONE) {
        return saved;
    }
    lock();
    getBackendSettings(api) = settings;
    if (service != nullptr && api == getSelectedBackend()) {
        MeshHost* host = service;
        post(host, [host] { configure(host); });
    }
    unlock();
    return ERROR_NONE;
}

error_t mesh_set_preset(const char* backend, const char* name) {
    const bool applies = presetsApply();
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    const size_t index = (api != nullptr) ? findPreset(api, name) : 0;
    if (api == nullptr || !applies || index >= getPresetCount(api) || strcmp(api->presets[index], name) != 0) {
        unlock();
        return ERROR_NOT_FOUND;
    }
    MeshBackendSettings settings = {};
    strncpy(settings.preset, name, MESH_NAME_MAX_LENGTH);
    unlock();
    return commitBackendSettings(api, settings);
}

error_t mesh_get_preset_region(const char* backend, const char* name, RadioRegion* region) {
    Device* radio = findRadio();
    const RadioRegion current = (radio != nullptr) ? radio_get_region(radio) : RADIO_REGION_UNSET;
    if (radio != nullptr) {
        device_put(radio);
    }
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    const size_t index = (api != nullptr) ? findPreset(api, name) : 0;
    error_t result = ERROR_NOT_FOUND;
    if (api != nullptr && index < getPresetCount(api) && strcmp(api->presets[index], name) == 0) {
        *region = current;
        result = (api->get_preset_region != nullptr) ? api->get_preset_region(index, current, region) : ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_get_preset(const char* backend, char* name, size_t name_size) {
    const bool applies = presetsApply();
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    error_t result = ERROR_NOT_FOUND;
    if (api != nullptr && applies && getPresetCount(api) > 0 && name_size > 0) {
        strncpy(name, api->presets[findPreset(api, getBackendSettings(api).preset)], name_size - 1);
        name[name_size - 1] = '\0';
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

/** Whether the whole channel of width @a bandwidth around @a frequency lies in one band of @a region */
static bool fitsRegion(RadioRegion region, int32_t frequency, int32_t bandwidth) {
    const RadioRegionBand* bands = nullptr;
    const size_t count = radio_get_region_bands(region, &bands);
    return std::any_of(bands, bands + count, [=](const RadioRegionBand& band) {
        return frequency - bandwidth / 2 >= band.frequency_min && frequency + bandwidth / 2 <= band.frequency_max;
    });
}

error_t mesh_set_radio_settings(const char* backend, const MeshRadioSettings* radio_settings) {
    const bool applies = presetsApply();
    Device* radio = findRadio();
    const RadioRegion region = (radio != nullptr) ? radio_get_region(radio) : RADIO_REGION_UNSET;
    if (radio != nullptr) {
        device_put(radio);
    }
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    if (api == nullptr) {
        unlock();
        return ERROR_NOT_FOUND;
    }
    MeshBackendSettings settings = getBackendSettings(api);
    settings.radio_settings = *radio_settings;
    if (applies && region != RADIO_REGION_UNSET) {
        // The radio rejects a channel outside the bands of the region, which would leave the mesh in the error state
        MeshRadioProfile profile {};
        if (getRadioProfile(api, nullptr, region, RADIO_MODULATION_LORA, findPreset(api, settings.preset), settings.radio_settings, &profile) == ERROR_NONE &&
            !fitsRegion(region, profile.frequency, profile.bandwidth)) {
            unlock();
            return ERROR_OUT_OF_RANGE;
        }
    }
    unlock();
    return commitBackendSettings(api, settings);
}

error_t mesh_get_radio_settings(const char* backend, MeshRadioSettings* radio_settings) {
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    error_t result = ERROR_NOT_FOUND;
    if (api != nullptr) {
        *radio_settings = getBackendSettings(api).radio_settings;
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_set_network_settings(const char* backend, const MeshNetworkSettings* network_settings) {
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    if (api == nullptr) {
        unlock();
        return ERROR_NOT_FOUND;
    }
    MeshBackendSettings settings = getBackendSettings(api);
    settings.network = *network_settings;
    unlock();
    return commitBackendSettings(api, settings);
}

error_t mesh_get_network_settings(const char* backend, MeshNetworkSettings* network_settings) {
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    error_t result = ERROR_NOT_FOUND;
    if (api != nullptr) {
        *network_settings = getBackendSettings(api).network;
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_get_network_parameters(const char* backend, MeshNetworkSettings* parameters) {
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    error_t result = ERROR_NOT_FOUND;
    if (api != nullptr) {
        const MeshBackendSettings settings = getBackendSettings(api);
        *parameters = getNetwork(api, findPreset(api, settings.preset), settings.network);
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_get_radio_parameters(const char* backend, MeshRadioSettings* parameters) {
    const bool applies = presetsApply();
    Device* radio = findRadio();
    const RadioRegion region = (radio != nullptr) ? radio_get_region(radio) : RADIO_REGION_UNSET;
    if (radio != nullptr) {
        device_put(radio);
    }
    lock();
    ensureSettingsLoaded();
    const MeshBackendApi* api = resolveBackend(backend);
    error_t result = ERROR_INVALID_STATE;
    if (api != nullptr && service != nullptr && service->api == api && service->backend != nullptr && service->state == MESH_STATE_ON &&
        service->profile.modulation == RADIO_MODULATION_LORA) {
        // The running backend knows its own state, e.g. the frequency slot that its primary channel selects
        parameters->frequency = service->profile.frequency;
        parameters->bandwidth = service->profile.bandwidth;
        parameters->spreading_factor = service->profile.spreading_factor;
        parameters->coding_rate = service->profile.coding_rate;
        parameters->power = service->profile.power;
        result = ERROR_NONE;
    } else if (api != nullptr && applies && region != RADIO_REGION_UNSET) {
        const MeshBackendSettings settings = getBackendSettings(api);
        MeshRadioProfile profile {};
        // Without a running backend there is no state, so a backend that depends on it uses its defaults
        result = getRadioProfile(api, nullptr, region, RADIO_MODULATION_LORA, findPreset(api, settings.preset), settings.radio_settings, &profile);
        if (result == ERROR_NONE) {
            parameters->frequency = profile.frequency;
            parameters->bandwidth = profile.bandwidth;
            parameters->spreading_factor = profile.spreading_factor;
            parameters->coding_rate = profile.coding_rate;
            parameters->power = profile.power;
        }
    }
    unlock();
    return result;
}

error_t mesh_send_text(const MeshAddress* to, uint8_t channel, const char* text, uint32_t* seq) {
    if (text == nullptr || strlen(text) > MESH_TEXT_MAX_LENGTH) {
        return ERROR_OUT_OF_RANGE;
    }

    lock();
    MeshHost* host = service;
    if (host == nullptr || host->state != MESH_STATE_ON) {
        unlock();
        return ERROR_INVALID_STATE;
    }

    MeshMessage message {};
    strncpy(message.backend, host->activeBackend, MESH_BACKEND_ID_MAX_LENGTH);
    message.outgoing = true;
    message.delivery = MESH_DELIVERY_QUEUED;
    if (to != nullptr) {
        message.peer = *to;
    }
    message.channel = channel;
    strncpy(message.sender_name, globals().settings.node_name, MESH_NAME_MAX_LENGTH);
    const int64_t now = (int64_t)time(nullptr);
    message.wall_time = (now >= MIN_VALID_WALL_TIME) ? now : 0;
    message.time_source = (now >= MIN_VALID_WALL_TIME) ? MESH_TIME_SOURCE_SYSTEM : MESH_TIME_SOURCE_NONE;
    message.uptime_ms = getMillis();
    message.snr = NAN;
    strncpy(message.text, text, MESH_TEXT_MAX_LENGTH);

    const error_t result = host->store.add(&message);
    if (result == ERROR_NONE) {
        emitEvent(MESH_EVENT_MESSAGE, host->state, message.seq, MESH_DELIVERY_QUEUED);
        const uint32_t messageSeq = message.seq;
        post(host, [host, messageSeq] { sendStored(host, messageSeq); });
        if (seq != nullptr) {
            *seq = message.seq;
        }
    }
    unlock();
    return result;
}

error_t mesh_cancel(uint32_t seq) {
    lock();
    MeshHost* host = service;
    if (host == nullptr) {
        unlock();
        return ERROR_INVALID_STATE;
    }
    MeshMessage message;
    if (host->store.get(seq, &message) != ERROR_NONE || !message.outgoing) {
        unlock();
        return ERROR_NOT_FOUND;
    }
    if (message.delivery != MESH_DELIVERY_QUEUED && message.delivery != MESH_DELIVERY_BLOCKED) {
        unlock();
        return ERROR_INVALID_STATE;
    }
    setDelivery(host, seq, MESH_DELIVERY_CANCELLED);
    post(host, [host, seq] {
        std::erase_if(host->txQueue, [seq](const Frame& frame) { return frame.seq == seq; });
    });
    unlock();
    return ERROR_NONE;
}

error_t mesh_raw_send(const char* backend, const uint8_t* data, size_t length) {
    if (length == 0 || length > RADIO_RX_MAX_PACKET_LENGTH) {
        return ERROR_OUT_OF_RANGE;
    }
    // The caller's buffers stay valid during this synchronous call
    error_t result = ERROR_INVALID_STATE;
    const error_t callResult = callOnServiceThread([&](MeshHost* host) {
        if (host->api != nullptr && strcmp(backend, host->api->id) == 0) {
            result = mesh_host_transmit(host, data, length, 0);
        }
    });
    return (callResult != ERROR_NONE) ? callResult : result;
}

error_t mesh_message_get(uint32_t seq, MeshMessage* message) {
    lock();
    const error_t result = (service != nullptr) ? service->store.get(seq, message) : ERROR_INVALID_STATE;
    unlock();
    return result;
}

error_t mesh_history_get(uint32_t before_seq, MeshMessage* messages, size_t max_count, size_t* count) {
    lock();
    error_t result = ERROR_INVALID_STATE;
    if (service != nullptr) {
        *count = service->store.getHistory(before_seq, messages, max_count);
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_conversation_get(const MeshAddress* peer, uint8_t channel, uint32_t before_seq, MeshMessage* messages, size_t max_count, size_t* count) {
    lock();
    error_t result = ERROR_INVALID_STATE;
    if (service != nullptr) {
        *count = service->store.getConversation(service->activeBackend, peer, channel, before_seq, messages, max_count);
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_get_contacts(MeshContact* contacts, size_t max_count, size_t* count) {
    lock();
    error_t result = ERROR_INVALID_STATE;
    if (service != nullptr) {
        *count = 0;
        for (const auto& contact : service->contacts) {
            if (*count < max_count && strncmp(contact.backend, service->activeBackend, MESH_BACKEND_ID_MAX_LENGTH) == 0) {
                contacts[(*count)++] = contact;
            }
        }
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_add_contact(const MeshAddress* address, const char* name) {
    if (address->length == 0 || address->length > MESH_ADDRESS_MAX_LENGTH) {
        return ERROR_INVALID_ARGUMENT;
    }
    bool valid = true;
    const error_t callResult = callOnServiceThread([&](MeshHost* host) {
        if (host->api != nullptr && host->api->is_valid_contact_address != nullptr) {
            valid = host->api->is_valid_contact_address(host->backend, address);
        }
    });
    if (callResult != ERROR_NONE) {
        return callResult;
    }
    if (!valid) {
        return ERROR_INVALID_ARGUMENT;
    }

    lock();
    error_t result = ERROR_INVALID_STATE;
    if (service != nullptr) {
        result = addContact(service, *address, name, true);
    }
    unlock();
    return result;
}

error_t mesh_remove_contact(const MeshAddress* address) {
    lock();
    error_t result = ERROR_INVALID_STATE;
    if (service != nullptr) {
        auto* contact = findContact(service, *address);
        if (contact == nullptr) {
            result = ERROR_NOT_FOUND;
        } else {
            service->contacts.erase(service->contacts.begin() + (contact - service->contacts.data()));
            saveContacts(service);
            emitEvent(MESH_EVENT_CONTACT, service->state, 0, MESH_DELIVERY_RECEIVED);
            result = ERROR_NONE;
        }
    }
    unlock();
    return result;
}

error_t mesh_get_contact_uri(const MeshAddress* contact, char* uri, size_t uri_size) {
    char name[MESH_NAME_MAX_LENGTH + 1] = {};
    lock();
    if (service == nullptr) {
        unlock();
        return ERROR_INVALID_STATE;
    }
    if (contact == nullptr) {
        strncpy(name, globals().settings.node_name, MESH_NAME_MAX_LENGTH);
    } else {
        const auto* saved = findContact(service, *contact);
        if (saved == nullptr) {
            unlock();
            return ERROR_NOT_FOUND;
        }
        strncpy(name, saved->name, MESH_NAME_MAX_LENGTH);
    }
    unlock();

    error_t result = ERROR_INVALID_STATE;
    const error_t callResult = callOnServiceThread([&](MeshHost* host) {
        if (host->api == nullptr) {
            return;
        }
        result = (host->api->get_contact_uri != nullptr)
            ? host->api->get_contact_uri(host->backend, contact, name, uri, uri_size)
            : ERROR_NOT_SUPPORTED;
    });
    return (callResult != ERROR_NONE) ? callResult : result;
}

error_t mesh_get_nodes(MeshNode* nodes, size_t max_count, size_t* count) {
    lock();
    error_t result = ERROR_INVALID_STATE;
    if (service != nullptr) {
        *count = std::min(max_count, service->nodes.size());
        std::copy_n(service->nodes.begin(), *count, nodes);
        result = ERROR_NONE;
    }
    unlock();
    return result;
}

error_t mesh_get_channels(MeshChannel* channels, size_t max_count, size_t* count) {
    error_t result = ERROR_INVALID_STATE;
    const error_t callResult = callOnServiceThread([&](MeshHost* host) {
        if (host->api == nullptr) {
            return;
        }
        result = (host->api->get_channels != nullptr)
            ? host->api->get_channels(host->backend, channels, max_count, count)
            : ERROR_NOT_SUPPORTED;
    });
    return (callResult != ERROR_NONE) ? callResult : result;
}

/** Tells the subscribers that the channels changed, when the change succeeded */
static void emitChannelChanged(error_t result) {
    if (result != ERROR_NONE) {
        return;
    }
    lock();
    if (service != nullptr) {
        emitEvent(MESH_EVENT_CHANNEL, service->state, 0, MESH_DELIVERY_RECEIVED);
    }
    unlock();
}

error_t mesh_set_channel(const MeshChannel* channel) {
    error_t result = ERROR_INVALID_STATE;
    const error_t callResult = callOnServiceThread([&](MeshHost* host) {
        if (host->api == nullptr) {
            return;
        }
        result = (host->api->set_channel != nullptr)
            ? host->api->set_channel(host->backend, channel)
            : ERROR_NOT_SUPPORTED;
    });
    const error_t finalResult = (callResult != ERROR_NONE) ? callResult : result;
    emitChannelChanged(finalResult);
    return finalResult;
}

error_t mesh_remove_channel(uint8_t index) {
    error_t result = ERROR_INVALID_STATE;
    const error_t callResult = callOnServiceThread([&](MeshHost* host) {
        if (host->api == nullptr) {
            return;
        }
        result = (host->api->remove_channel != nullptr)
            ? host->api->remove_channel(host->backend, index)
            : ERROR_NOT_SUPPORTED;
    });
    const error_t finalResult = (callResult != ERROR_NONE) ? callResult : result;
    emitChannelChanged(finalResult);
    return finalResult;
}

// endregion

// region Events

error_t mesh_event_subscribe(MeshEventSubscription* sub, TaskEventGroup* event_group) {
    uint32_t bit;
    const error_t claimed = task_event_group_claim_bit(event_group, &bit);
    if (claimed != ERROR_NONE) {
        return claimed;
    }

    lock();
    for (auto* existing = globals().subscriptions; existing != nullptr; existing = existing->internal.next) {
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
    sub->internal.next = globals().subscriptions;
    globals().subscriptions = sub;
    unlock();
    return ERROR_NONE;
}

error_t mesh_event_unsubscribe(MeshEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;
    lock();
    for (auto** link = &globals().subscriptions; *link != nullptr; link = &(*link)->internal.next) {
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

error_t mesh_event_poll(MeshEventSubscription* sub, MeshEvent* event) {
    lock();
    const bool hasEvent = sub->internal.count > 0;
    if (hasEvent) {
        *event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % MESH_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    unlock();
    return hasEvent ? ERROR_NONE : ERROR_TIMEOUT;
}

// endregion

} // extern "C"
