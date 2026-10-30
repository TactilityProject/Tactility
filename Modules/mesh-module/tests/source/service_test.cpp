#include "doctest.h"

#include <mesh/backend.h>
#include <mesh/mesh.h>
#include <mesh/mesh_service.h>
#include <mesh/mesh_settings.h>
#include <mesh/mesh_store.h>

#include <service/manager.h>

#include <tactility/concurrent/thread.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/drivers/radio.h>
#include <tactility/filesystem/file_system.h>
#include <tactility/filesystem/fs.h>
#include <tactility/paths.h>
#include <tactility/properties_file.h>

#include <service/paths.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include <unistd.h>

namespace {

// region Fake data filesystem, so the service can store messages in a temporary directory

error_t fakeMount(void*) { return ERROR_NONE; }
error_t fakeUnmount(void*) { return ERROR_NONE; }
bool fakeIsMounted(void*) { return false; }

error_t fakeGetPath(void* data, char* out_path, size_t out_path_size) {
    const char* path = static_cast<const char*>(data);
    if (strlen(path) + 1 > out_path_size) {
        return ERROR_BUFFER_OVERFLOW;
    }
    strcpy(out_path, path);
    return ERROR_NONE;
}

const FileSystemApi fakeDataApi = {
    .mount = fakeMount,
    .unmount = fakeUnmount,
    .is_mounted = fakeIsMounted,
    .get_path = fakeGetPath,
};

// endregion

// region Loopback backend: a frame is a 1-byte channel followed by the text

int startCount = 0;

void* loopbackCreate(MeshHost* host) {
    // Backends load their state here
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (mesh_host_get_data_path(host, "state.bin", path, sizeof(path)) != ERROR_NONE) {
        return nullptr;
    }
    return host;
}
void loopbackDestroy(void*) {}

const char* const loopbackPresets[] = { "Default", "Other", "Wide", nullptr };

error_t loopbackGetRadioProfile(void*, RadioRegion region, RadioModulation modulation, size_t preset, MeshRadioProfile* profile) {
    if (modulation == RADIO_MODULATION_ESPNOW) {
        *profile = { .modulation = RADIO_MODULATION_ESPNOW, .power = 20, .channel = 6 };
        return ERROR_NONE;
    }
    *profile = {
        .modulation = RADIO_MODULATION_LORA,
        .frequency = (region == RADIO_REGION_US_915) ? 915000000 : 869525000,
        .bandwidth = (preset == 2) ? 500000 : 250000,
        .spreading_factor = (preset == 1) ? 7 : 11,
        .coding_rate = 5,
        .sync_word = 0x2B,
        .preamble_length = 16,
        .power = 14,
    };
    return ERROR_NONE;
}

// The "Other" preset stands for one that sets its own frequency in the US band
error_t loopbackGetPresetRegion(size_t preset, RadioRegion current, RadioRegion* region) {
    *region = (preset == 1) ? RADIO_REGION_US_915 : current;
    return ERROR_NONE;
}

// The "Other" preset stands for one with a larger path hash size and a scope
void loopbackGetPresetNetwork(size_t preset, MeshNetworkSettings* network) {
    *network = { .path_hash_size = (preset == 1) ? 2 : 1 };
    if (preset == 1) {
        strcpy(network->region_scope, "nl");
    }
}

void loopbackOnStart(void*) { startCount++; }

// Channel 0xFF stands for frames the backend can't read, 0xFE for adverts
constexpr uint8_t UNREADABLE_CHANNEL = 0xFF;
constexpr uint8_t ADVERT_CHANNEL = 0xFE;

bool loopbackOnReceive(void* backend, const uint8_t* data, size_t length, float rssi, float snr) {
    auto* host = static_cast<MeshHost*>(backend);
    if (length < 1 || data[0] == UNREADABLE_CHANNEL) {
        return false;
    }
    if (data[0] == ADVERT_CHANNEL) {
        MeshAddress from = { .length = 1, .bytes = { 0x43 } };
        mesh_host_advert_heard(host, &from, "advertiser", rssi, snr);
        return true;
    }
    std::string text(reinterpret_cast<const char*>(data + 1), length - 1);
    MeshAddress from = { .length = 1, .bytes = { 0x42 } };
    mesh_host_node_heard(host, &from, "peer", rssi, snr);
    mesh_host_receive_text(host, &from, data[0], "peer", text.c_str(), rssi, snr, 0);
    return true;
}

void loopbackOnTick(void*) {}

error_t loopbackGetContactUri(void*, const MeshAddress* contact, const char* name, char* uri, size_t uriSize) {
    snprintf(uri, uriSize, "loopback://%s/%02x", name, contact != nullptr ? contact->bytes[0] : 0);
    return ERROR_NONE;
}

bool loopbackIsValidContactAddress(void*, const MeshAddress* address) { return address->length == 1; }

std::atomic<int> nameChangeCount = 0;
void loopbackOnNodeNameChanged(void*) { nameChangeCount++; }

error_t loopbackSendText(void* backend, uint32_t seq, const MeshAddress*, uint8_t channel, const char* text) {
    uint8_t frame[1 + MESH_TEXT_MAX_LENGTH];
    frame[0] = channel;
    const size_t length = strlen(text);
    memcpy(frame + 1, text, length);
    return mesh_host_transmit(static_cast<MeshHost*>(backend), frame, length + 1, seq);
}

const MeshBackendApi loopbackApi = {
    .id = "loopback",
    .create = loopbackCreate,
    .destroy = loopbackDestroy,
    .presets = loopbackPresets,
    .get_radio_profile = loopbackGetRadioProfile,
    .get_preset_region = loopbackGetPresetRegion,
    .get_preset_network = loopbackGetPresetNetwork,
    .on_start = loopbackOnStart,
    .on_receive = loopbackOnReceive,
    .on_tick = loopbackOnTick,
    .send_text = loopbackSendText,
    .get_channels = nullptr,
    .set_channel = nullptr,
    .get_contact_uri = loopbackGetContactUri,
    .is_valid_contact_address = loopbackIsValidContactAddress,
    .on_node_name_changed = loopbackOnNodeNameChanged,
};

// endregion

// region A radio that only does ESP-NOW

struct EspNowFake {
    bool enabled = false;
    RadioModulation modulation = RADIO_MODULATION_NONE;
    int32_t channel = 0;
    int32_t power = 0;
};
EspNowFake espNowFake;

error_t espNowGetState(Device*, RadioState* state) {
    *state = espNowFake.enabled ? RADIO_STATE_ON : RADIO_STATE_OFF;
    return ERROR_NONE;
}
error_t espNowSetEnabled(Device*, bool enabled) {
    espNowFake.enabled = enabled;
    return ERROR_NONE;
}
error_t espNowSetModulation(Device*, RadioModulation modulation) {
    if (modulation != RADIO_MODULATION_ESPNOW) {
        return ERROR_NOT_SUPPORTED;
    }
    espNowFake.modulation = modulation;
    return ERROR_NONE;
}
error_t espNowGetModulation(Device*, RadioModulation* modulation) {
    *modulation = espNowFake.modulation;
    return ERROR_NONE;
}
bool espNowCanDo(Device*, RadioModulation modulation) { return modulation == RADIO_MODULATION_ESPNOW; }
error_t espNowSetParameter(Device*, RadioParameter parameter, int32_t value) {
    if (parameter == RADIO_PARAMETER_CHANNEL) {
        espNowFake.channel = value;
        return ERROR_NONE;
    }
    if (parameter == RADIO_PARAMETER_POWER) {
        espNowFake.power = value;
        return ERROR_NONE;
    }
    return ERROR_NOT_SUPPORTED;
}
error_t espNowGetParameter(Device*, RadioParameter, int32_t*) { return ERROR_NOT_SUPPORTED; }
error_t espNowTransmit(Device*, const uint8_t*, size_t, RadioTxId*) { return ERROR_NONE; }

const RadioApi espNowApi = {
    .get_radio_state = espNowGetState,
    .set_enabled = espNowSetEnabled,
    .set_modulation = espNowSetModulation,
    .get_modulation = espNowGetModulation,
    .can_transmit = espNowCanDo,
    .can_receive = espNowCanDo,
    .set_parameter = espNowSetParameter,
    .get_parameter = espNowGetParameter,
    .transmit = espNowTransmit,
    .get_rssi = nullptr,
    .is_receiving = nullptr,
};

Driver espNowDriver = {
    .name = "mesh_test_espnow_driver",
    .compatible = (const char*[]) { "mesh_test,espnow", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &espNowApi,
    .device_type = &RADIO_TYPE,
    .internal = nullptr,
};

// endregion

bool waitFor(const std::function<bool()>& condition) {
    for (int i = 0; i < 300; i++) {
        if (condition()) {
            return true;
        }
        delay_millis(10);
    }
    return false;
}

MeshDeliveryState getDelivery(uint32_t seq) {
    MeshMessage message {};
    mesh_message_get(seq, &message);
    return message.delivery;
}

} // namespace

TEST_CASE("mesh service sends and receives through the active backend") {
    char dataPath[] = "/tmp/mesh_service_test_XXXXXX";
    REQUIRE_NE(mkdtemp(dataPath), nullptr);
    FileSystem* dataFs = file_system_add("data", &fakeDataApi, dataPath);

    // The service uses the first radio, the second one plays the remote node
    static Device local { .name = "radio_local", .config = nullptr, .parent = nullptr };
    static Device remote { .name = "radio_remote", .config = nullptr, .parent = nullptr };
    CHECK_FALSE(mesh_is_available());
    REQUIRE_EQ(device_construct_add(&local, "mock,radio"), ERROR_NONE);
    REQUIRE_EQ(device_construct_add(&remote, "mock,radio"), ERROR_NONE);
    REQUIRE_EQ(device_start(&local), ERROR_NONE);
    REQUIRE_EQ(device_start(&remote), ERROR_NONE);
    CHECK(mesh_is_available());

    REQUIRE_EQ(mesh_backend_register(&loopbackApi), ERROR_NONE);
    CHECK_EQ(mesh_backend_register(&loopbackApi), ERROR_ALREADY_EXISTS);
    for (const char* id : { "", "../escape", "a/b", "a.b", "a b" }) {
        MeshBackendApi invalidApi = loopbackApi;
        invalidApi.id = id;
        CHECK_EQ(mesh_backend_register(&invalidApi), ERROR_INVALID_ARGUMENT);
    }
    REQUIRE_EQ(service_manager_add(&mesh_service_manifest, false), ERROR_NONE);

    SUBCASE("messages and contacts that all backends shared move to their backends") {
        MeshBackendApi spareApi = loopbackApi;
        spareApi.id = "spare";
        REQUIRE_EQ(mesh_backend_register(&spareApi), ERROR_NONE);

        // The layout of before: one store and one contacts file, with the backend in each record
        char storePath[FILE_MAX_PATH_STRING_LENGTH];
        char contactsPath[FILE_MAX_PATH_STRING_LENGTH];
        REQUIRE_EQ(service_paths_get_user_data_path(MESH_SERVICE_ID, "store", storePath, sizeof(storePath)), ERROR_NONE);
        REQUIRE_EQ(service_paths_get_user_data_path(MESH_SERVICE_ID, "contacts.bin", contactsPath, sizeof(contactsPath)), ERROR_NONE);
        {
            MeshStore shared;
            REQUIRE_EQ(shared.open(storePath), ERROR_NONE);
            for (const char* backend : { "loopback", "spare", "loopback", "../escape" }) {
                MeshMessage message = {};
                strcpy(message.backend, backend);
                strcpy(message.text, backend);
                REQUIRE_EQ(shared.add(&message), ERROR_NONE);
            }
            shared.close();
        }
        {
            FILE* file = fopen(contactsPath, "wb");
            REQUIRE_NE(file, nullptr);
            for (const char* backend : { "loopback", "spare" }) {
                MeshContact contact = {};
                strcpy(contact.backend, backend);
                contact.address = { .length = 1, .bytes = { 0x42 } };
                strcpy(contact.name, backend);
                REQUIRE_EQ(fwrite(&contact, sizeof(contact), 1, file), 1);
            }
            fclose(file);
        }

        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE_EQ(mesh_set_backend("loopback"), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        // The shared files are gone, and each backend has its own
        CHECK_FALSE(directory_exists(storePath));
        CHECK_EQ(access(contactsPath, F_OK), -1);

        MeshMessage messages[4];
        size_t count = 0;
        REQUIRE_EQ(mesh_history_get(0, messages, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 2);
        for (size_t i = 0; i < count; i++) {
            CHECK_EQ(std::string(messages[i].backend), "loopback");
        }
        MeshContact contacts[4];
        REQUIRE_EQ(mesh_get_contacts(contacts, 4, &count), ERROR_NONE);
        REQUIRE_EQ(count, 1);
        CHECK_EQ(std::string(contacts[0].name), "loopback");

        REQUIRE_EQ(mesh_set_backend("spare"), ERROR_NONE);
        REQUIRE(waitFor([] {
            char id[MESH_BACKEND_ID_MAX_LENGTH + 1];
            return mesh_get_state() == MESH_STATE_ON && mesh_get_backend(id, sizeof(id)) == ERROR_NONE && std::string(id) == "spare";
        }));
        REQUIRE_EQ(mesh_history_get(0, messages, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 1);
        CHECK_EQ(std::string(messages[0].text), "spare");
        REQUIRE_EQ(mesh_get_contacts(contacts, 4, &count), ERROR_NONE);
        REQUIRE_EQ(count, 1);
        CHECK_EQ(std::string(contacts[0].name), "spare");

        REQUIRE_EQ(mesh_set_backend("loopback"), ERROR_NONE);
        REQUIRE(waitFor([] {
            char id[MESH_BACKEND_ID_MAX_LENGTH + 1];
            return mesh_get_state() == MESH_STATE_ON && mesh_get_backend(id, sizeof(id)) == ERROR_NONE && std::string(id) == "loopback";
        }));
        REQUIRE_EQ(mesh_backend_unregister(&spareApi), ERROR_NONE);
    }

    SUBCASE("without a region the radio stays off") {
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        CHECK(waitFor([] { return mesh_get_state() == MESH_STATE_NO_REGION; }));
        CHECK_EQ(mesh_send_text(nullptr, 0, "hi", nullptr), ERROR_INVALID_STATE);

        // Setting a region later brings the service up
        CHECK_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        CHECK(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
    }

    SUBCASE("a radio that starts after the service is picked up") {
        REQUIRE_EQ(device_stop(&local), ERROR_NONE);
        REQUIRE_EQ(device_stop(&remote), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        CHECK(waitFor([] { return mesh_get_state() == MESH_STATE_NO_RADIO; }));

        REQUIRE_EQ(device_start(&local), ERROR_NONE);
        REQUIRE_EQ(device_start(&remote), ERROR_NONE);
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        CHECK(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
    }

    SUBCASE("selecting a preset reconfigures the radio") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        char name[MESH_NAME_MAX_LENGTH + 1];
        CHECK_EQ(mesh_get_preset_count(nullptr), 3);
        CHECK_EQ(mesh_get_preset_name(nullptr, 1, name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Other");
        CHECK_EQ(mesh_get_preset_name(nullptr, 3, name, sizeof(name)), ERROR_NOT_FOUND);
        CHECK_EQ(mesh_get_preset(nullptr, name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Default");

        CHECK_EQ(mesh_set_preset(nullptr, "Missing"), ERROR_NOT_FOUND);
        REQUIRE_EQ(mesh_set_preset(nullptr, "Other"), ERROR_NONE);
        CHECK_EQ(mesh_get_preset(nullptr, name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Other");
        CHECK(waitFor([] {
            int32_t spreadingFactor = 0;
            return radio_get_parameter(&local, RADIO_PARAMETER_SPREADING_FACTOR, &spreadingFactor) == ERROR_NONE && spreadingFactor == 7;
        }));
        CHECK(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
        REQUIRE_EQ(mesh_set_preset(nullptr, "Default"), ERROR_NONE);
    }

    SUBCASE("each backend has its own messages and contacts") {
        MeshBackendApi spareApi = loopbackApi;
        spareApi.id = "spare";
        REQUIRE_EQ(mesh_backend_register(&spareApi), ERROR_NONE);
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE_EQ(mesh_set_backend("loopback"), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        const MeshAddress alice = { .length = 1, .bytes = { 0x42 } };
        uint32_t seq = 0;
        REQUIRE_EQ(mesh_send_text(nullptr, 0, "from loopback", &seq), ERROR_NONE);
        REQUIRE_EQ(mesh_add_contact(&alice, "alice"), ERROR_NONE);

        const auto countHistory = [] {
            MeshMessage messages[4];
            size_t count = 0;
            return mesh_history_get(0, messages, 4, &count) == ERROR_NONE ? count : 99;
        };
        const auto countConversation = [] {
            MeshMessage messages[4];
            size_t count = 0;
            return mesh_conversation_get(nullptr, 0, 0, messages, 4, &count) == ERROR_NONE ? count : 99;
        };
        const auto countContacts = [] {
            MeshContact contacts[4];
            size_t count = 0;
            return mesh_get_contacts(contacts, 4, &count) == ERROR_NONE ? count : 99;
        };
        CHECK_EQ(countHistory(), 1);
        CHECK_EQ(countConversation(), 1);
        CHECK_EQ(countContacts(), 1);

        TaskEventGroup storageGroup {};
        task_event_group_construct(&storageGroup);
        MeshEventSubscription storageEvents {};
        REQUIRE_EQ(mesh_event_subscribe(&storageEvents, &storageGroup), ERROR_NONE);
        // Events of the setup above
        MeshEvent storageEvent;
        while (mesh_event_poll(&storageEvents, &storageEvent) == ERROR_NONE) {}

        // Another backend starts with nothing, and what it stores stays with it
        REQUIRE_EQ(mesh_set_backend("spare"), ERROR_NONE);
        REQUIRE(waitFor([] {
            char id[MESH_BACKEND_ID_MAX_LENGTH + 1];
            return mesh_get_state() == MESH_STATE_ON && mesh_get_backend(id, sizeof(id)) == ERROR_NONE && std::string(id) == "spare";
        }));
        // Selecting a backend tells consumers that the contacts and channels changed
        bool contactsChanged = false;
        bool channelsChanged = false;
        while (mesh_event_poll(&storageEvents, &storageEvent) == ERROR_NONE) {
            contactsChanged |= storageEvent.type == MESH_EVENT_CONTACT;
            channelsChanged |= storageEvent.type == MESH_EVENT_CHANNEL;
        }
        CHECK(contactsChanged);
        CHECK(channelsChanged);
        CHECK_EQ(mesh_event_unsubscribe(&storageEvents), ERROR_NONE);
        task_event_group_destruct(&storageGroup);
        CHECK_EQ(countHistory(), 0);
        CHECK_EQ(countConversation(), 0);
        CHECK_EQ(countContacts(), 0);
        REQUIRE_EQ(mesh_send_text(nullptr, 0, "from spare", &seq), ERROR_NONE);
        CHECK_EQ(countHistory(), 1);
        MeshMessage message;
        size_t count = 0;
        REQUIRE_EQ(mesh_history_get(0, &message, 1, &count), ERROR_NONE);
        CHECK_EQ(std::string(message.text), "from spare");

        REQUIRE_EQ(mesh_set_backend("loopback"), ERROR_NONE);
        REQUIRE(waitFor([] {
            char id[MESH_BACKEND_ID_MAX_LENGTH + 1];
            return mesh_get_state() == MESH_STATE_ON && mesh_get_backend(id, sizeof(id)) == ERROR_NONE && std::string(id) == "loopback";
        }));
        CHECK_EQ(countHistory(), 1);
        REQUIRE_EQ(mesh_history_get(0, &message, 1, &count), ERROR_NONE);
        CHECK_EQ(std::string(message.text), "from loopback");
        CHECK_EQ(countContacts(), 1);

        REQUIRE_EQ(mesh_backend_unregister(&spareApi), ERROR_NONE);
    }

    SUBCASE("a backend that isn't selected can be configured") {
        MeshBackendApi spareApi = loopbackApi;
        spareApi.id = "spare";
        REQUIRE_EQ(mesh_backend_register(&spareApi), ERROR_NONE);
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE_EQ(mesh_set_backend("loopback"), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        char name[MESH_NAME_MAX_LENGTH + 1];
        REQUIRE_EQ(mesh_set_preset("spare", "Other"), ERROR_NONE);
        const MeshRadioSettings settings = { .power = 5 };
        REQUIRE_EQ(mesh_set_radio_settings("spare", &settings), ERROR_NONE);

        // The selected backend keeps its own settings, and the radio keeps running with them
        CHECK_EQ(mesh_get_preset("spare", name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Other");
        CHECK_EQ(mesh_get_preset("loopback", name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Default");
        MeshRadioSettings stored = {};
        CHECK_EQ(mesh_get_radio_settings("loopback", &stored), ERROR_NONE);
        CHECK_EQ(stored.power, 0);
        CHECK_EQ(mesh_get_radio_settings("spare", &stored), ERROR_NONE);
        CHECK_EQ(stored.power, 5);
        int32_t spreadingFactor = 0;
        CHECK_EQ(radio_get_parameter(&local, RADIO_PARAMETER_SPREADING_FACTOR, &spreadingFactor), ERROR_NONE);
        CHECK_EQ(spreadingFactor, 11);

        // The parameters of the backend that isn't running follow its preset and settings
        MeshRadioSettings parameters = {};
        CHECK_EQ(mesh_get_radio_parameters("spare", &parameters), ERROR_NONE);
        CHECK_EQ(parameters.spreading_factor, 7);
        CHECK_EQ(parameters.power, 5);
        CHECK_EQ(mesh_get_preset("missing", name, sizeof(name)), ERROR_NOT_FOUND);

        REQUIRE_EQ(mesh_set_preset("spare", "Default"), ERROR_NONE);
        REQUIRE_EQ(mesh_backend_unregister(&spareApi), ERROR_NONE);
    }

    SUBCASE("network parameters follow the preset and the settings") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        MeshNetworkSettings network = {};
        CHECK_EQ(mesh_get_network_parameters(nullptr, &network), ERROR_NONE);
        CHECK_EQ(network.path_hash_size, 1);
        CHECK_EQ(std::string(network.region_scope), "");

        REQUIRE_EQ(mesh_set_preset(nullptr, "Other"), ERROR_NONE);
        CHECK_EQ(mesh_get_network_parameters(nullptr, &network), ERROR_NONE);
        CHECK_EQ(network.path_hash_size, 2);
        CHECK_EQ(std::string(network.region_scope), "nl");

        // A setting replaces the preset's value, and the scope can be turned off
        MeshNetworkSettings settings = { .path_hash_size = 3 };
        strcpy(settings.region_scope, MESH_REGION_SCOPE_NONE);
        REQUIRE_EQ(mesh_set_network_settings(nullptr, &settings), ERROR_NONE);
        CHECK_EQ(mesh_get_network_parameters(nullptr, &network), ERROR_NONE);
        CHECK_EQ(network.path_hash_size, 3);
        CHECK_EQ(std::string(network.region_scope), "");

        // The running backend sees them after the radio was configured again
        CHECK(waitFor([] {
            MeshNetworkSettings hosted = {};
            return mesh_get_network_settings(nullptr, &hosted) == ERROR_NONE && hosted.path_hash_size == 3;
        }));

        // Selecting a preset clears the settings
        REQUIRE_EQ(mesh_set_preset(nullptr, "Default"), ERROR_NONE);
        CHECK_EQ(mesh_get_network_settings(nullptr, &settings), ERROR_NONE);
        CHECK_EQ(settings.path_hash_size, 0);
        CHECK_EQ(std::string(settings.region_scope), "");
        CHECK_EQ(mesh_get_network_settings("missing", &settings), ERROR_NOT_FOUND);
    }

    SUBCASE("a frequency outside the region is rejected") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        MeshRadioSettings settings = { .frequency = 915000000 };
        CHECK_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_OUT_OF_RANGE);
        // A bandwidth that makes the channel leave the band is rejected too
        settings = { .bandwidth = 500000 };
        CHECK_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_OUT_OF_RANGE);

        // Nothing was stored, and the radio keeps running
        MeshRadioSettings stored = { .power = 1 };
        CHECK_EQ(mesh_get_radio_settings(nullptr, &stored), ERROR_NONE);
        CHECK_EQ(stored.frequency, 0);
        CHECK_EQ(stored.bandwidth, 0);
        CHECK_EQ(mesh_get_state(), MESH_STATE_ON);

        settings = { .frequency = 869525000 };
        CHECK_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_NONE);
        const MeshRadioSettings none = {};
        REQUIRE_EQ(mesh_set_radio_settings(nullptr, &none), ERROR_NONE);
    }

    SUBCASE("a preset that the region doesn't fit isn't selected") {
        char name[MESH_NAME_MAX_LENGTH + 1];
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        // The 500 kHz of "Wide" doesn't fit the 250 kHz band, and the radio stays as it was
        CHECK_EQ(mesh_set_preset(nullptr, "Wide"), ERROR_NOT_SUPPORTED);
        CHECK_EQ(mesh_get_preset(nullptr, name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Default");
        CHECK_EQ(mesh_get_state(), MESH_STATE_ON);

        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_US_915), ERROR_NONE);
        CHECK(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
        CHECK_EQ(mesh_set_preset(nullptr, "Wide"), ERROR_NONE);
        CHECK(waitFor([] {
            int32_t bandwidth = 0;
            return radio_get_parameter(&local, RADIO_PARAMETER_BANDWIDTH, &bandwidth) == ERROR_NONE && bandwidth == 500000;
        }));
        REQUIRE_EQ(mesh_set_preset(nullptr, "Default"), ERROR_NONE);
    }

    SUBCASE("a path hash size outside the range is rejected") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
        MeshNetworkSettings settings = { .path_hash_size = 4 };
        CHECK_EQ(mesh_set_network_settings(nullptr, &settings), ERROR_INVALID_ARGUMENT);
        settings.path_hash_size = -1;
        CHECK_EQ(mesh_set_network_settings(nullptr, &settings), ERROR_INVALID_ARGUMENT);
        MeshNetworkSettings stored = { .path_hash_size = 9 };
        CHECK_EQ(mesh_get_network_settings(nullptr, &stored), ERROR_NONE);
        CHECK_EQ(stored.path_hash_size, 0);

        settings.path_hash_size = MESH_PATH_HASH_SIZE_MAX;
        CHECK_EQ(mesh_set_network_settings(nullptr, &settings), ERROR_NONE);
        MeshNetworkSettings parameters = {};
        CHECK_EQ(mesh_get_network_parameters(nullptr, &parameters), ERROR_NONE);
        CHECK_EQ(parameters.path_hash_size, MESH_PATH_HASH_SIZE_MAX);
        settings.path_hash_size = 0;
        CHECK_EQ(mesh_set_network_settings(nullptr, &settings), ERROR_NONE);
    }

    SUBCASE("a preset reports the region it needs") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
        RadioRegion region = RADIO_REGION_UNSET;
        CHECK_EQ(mesh_get_preset_region(nullptr, "Other", &region), ERROR_NONE);
        CHECK_EQ(region, RADIO_REGION_US_915);
        CHECK_EQ(mesh_get_preset_region(nullptr, "Default", &region), ERROR_NONE);
        CHECK_EQ(region, RADIO_REGION_EU_868);
        CHECK_EQ(mesh_get_preset_region(nullptr, "Missing", &region), ERROR_NOT_FOUND);
    }

    SUBCASE("the power is limited to what the band of the channel allows") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        // 869.525 MHz is in the 27 dBm band of EU_868, and 868.1 MHz in the 14 dBm band
        MeshRadioSettings settings = { .power = 40 };
        CHECK_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_OUT_OF_RANGE);
        settings = { .frequency = 868100000, .power = 20 };
        CHECK_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_OUT_OF_RANGE);

        settings = { .power = 27 };
        REQUIRE_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));
        int32_t power = 0;
        CHECK_EQ(radio_get_parameter(&local, RADIO_PARAMETER_POWER, &power), ERROR_NONE);
        CHECK_EQ(power, 27);

        settings = { .frequency = 868100000, .power = 14 };
        REQUIRE_EQ(mesh_set_radio_settings(nullptr, &settings), ERROR_NONE);
        CHECK(waitFor([] {
            int32_t current = 0;
            return radio_get_parameter(&local, RADIO_PARAMETER_POWER, &current) == ERROR_NONE && current == 14;
        }));
        CHECK_EQ(mesh_get_state(), MESH_STATE_ON);

        // Without a setting, the power of the preset is limited to the band, here 14 dBm of the preset to 14 dBm
        const MeshRadioSettings none = {};
        REQUIRE_EQ(mesh_set_radio_settings(nullptr, &none), ERROR_NONE);
    }

    SUBCASE("a new node name is announced by the backend") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        // Without a chosen name, a random one is made up once
        char generated[MESH_NAME_MAX_LENGTH + 1];
        CHECK_EQ(mesh_get_node_name(generated, sizeof(generated)), ERROR_NONE);
        const std::string generatedName = generated;
        REQUIRE_EQ(generatedName.size(), std::string("TactilityUser-").size() + 8);
        CHECK_EQ(generatedName.substr(0, 14), "TactilityUser-");
        CHECK_EQ(generatedName.substr(14).find_first_not_of("0123456789abcdef"), std::string::npos);
        // The service keeps its settings for the whole process, so the data folder of this test is read directly
        MeshSettings first;
        MeshSettings second;
        mesh_settings_load(&first);
        mesh_settings_load(&second);
        CHECK_EQ(std::string(first.node_name).substr(0, 14), "TactilityUser-");
        CHECK_EQ(std::string(first.node_name), std::string(second.node_name));

        const int before = nameChangeCount;
        CHECK_EQ(mesh_set_node_name(""), ERROR_INVALID_ARGUMENT);
        CHECK_EQ(mesh_set_node_name("Ken's deck"), ERROR_NONE);
        char name[MESH_NAME_MAX_LENGTH + 1];
        CHECK_EQ(mesh_get_node_name(name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "Ken's deck");
        CHECK(waitFor([before] { return nameChangeCount > before; }));
    }

    SUBCASE("the radio can be selected") {
        static Device espNow { .name = "radio_espnow", .config = nullptr, .parent = nullptr };
        espNowFake = {};
        REQUIRE_EQ(driver_construct_add(&espNowDriver), ERROR_NONE);
        REQUIRE_EQ(device_construct_add(&espNow, "mesh_test,espnow"), ERROR_NONE);
        REQUIRE_EQ(device_start(&espNow), ERROR_NONE);
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        // The default is the first radio that isn't ESP-NOW
        CHECK_EQ(mesh_get_radio_count(), 3);
        char name[MESH_NAME_MAX_LENGTH + 1];
        CHECK_EQ(mesh_get_radio_name(2, name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "radio_espnow");
        CHECK_EQ(mesh_get_radio_name(3, name, sizeof(name)), ERROR_NOT_FOUND);
        MeshRadioKind kind;
        CHECK_EQ(mesh_get_radio_kind(0, &kind), ERROR_NONE);
        CHECK_EQ(kind, MESH_RADIO_LORA);
        CHECK_EQ(mesh_get_radio_kind(2, &kind), ERROR_NONE);
        CHECK_EQ(kind, MESH_RADIO_ESPNOW);
        CHECK_EQ(mesh_get_radio_kind(3, &kind), ERROR_NOT_FOUND);
        CHECK_EQ(mesh_get_radio(name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "radio_local");
        CHECK_EQ(mesh_get_preset_count(nullptr), 3);
        CHECK_FALSE(espNowFake.enabled);

        CHECK_EQ(mesh_set_radio("missing"), ERROR_NOT_FOUND);

        // A radio that doesn't do LoRa gets the ESP-NOW profile of the backend, and has no presets
        REQUIRE_EQ(mesh_set_radio("radio_espnow"), ERROR_NONE);
        CHECK(waitFor([] { return espNowFake.enabled && mesh_get_state() == MESH_STATE_ON; }));
        CHECK_EQ(espNowFake.modulation, RADIO_MODULATION_ESPNOW);
        CHECK_EQ(espNowFake.channel, 6);
        CHECK_EQ(espNowFake.power, 20);
        CHECK_EQ(mesh_get_radio(name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "radio_espnow");
        CHECK_EQ(mesh_get_preset_count(nullptr), 0);
        CHECK_EQ(radio_acquire(&local, "someone-else"), ERROR_NONE);
        CHECK_EQ(radio_release(&local), ERROR_NONE);

        // Back to the default
        REQUIRE_EQ(mesh_set_radio(""), ERROR_NONE);
        CHECK(waitFor([] { return !espNowFake.enabled && mesh_get_state() == MESH_STATE_ON; }));
        CHECK_EQ(mesh_get_radio(name, sizeof(name)), ERROR_NONE);
        CHECK_EQ(std::string(name), "radio_local");
        CHECK_EQ(mesh_get_preset_count(nullptr), 3);

        REQUIRE_EQ(service_manager_stop(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE_EQ(radio_set_region(&espNow, RADIO_REGION_UNSET), ERROR_NONE);
        REQUIRE_EQ(device_stop(&espNow), ERROR_NONE);
        REQUIRE_EQ(device_remove(&espNow), ERROR_NONE);
        REQUIRE_EQ(device_destruct(&espNow), ERROR_NONE);
        REQUIRE_EQ(driver_remove_destruct(&espNowDriver), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
    }

    SUBCASE("concurrent synchronous calls finish") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        const auto caller = [](void*) -> int32_t {
            for (int i = 0; i < 200; i++) {
                MeshChannel channels[1];
                size_t count = 0;
                mesh_get_channels(channels, 1, &count);
                mesh_get_state();
            }
            return 0;
        };
        Thread* threads[4];
        for (auto& thread : threads) {
            thread = thread_alloc_full("mesh_caller", 4096, caller, nullptr, -1);
            REQUIRE_EQ(thread_start(thread), ERROR_NONE);
        }
        for (auto* thread : threads) {
            CHECK_EQ(thread_join(thread, pdMS_TO_TICKS(5000), 1), ERROR_NONE);
            thread_free(thread);
        }
    }

    SUBCASE("messages travel both ways") {
        REQUIRE_EQ(radio_set_region(&local, RADIO_REGION_EU_868), ERROR_NONE);
        REQUIRE_EQ(service_manager_start(MESH_SERVICE_ID), ERROR_NONE);
        REQUIRE(waitFor([] { return mesh_get_state() == MESH_STATE_ON; }));

        char backend[MESH_BACKEND_ID_MAX_LENGTH + 1];
        CHECK_EQ(mesh_get_backend(backend, sizeof(backend)), ERROR_NONE);
        CHECK_EQ(std::string(backend), "loopback");

        // The service owns the radio
        CHECK_EQ(radio_acquire(&local, "someone-else"), ERROR_RESOURCE_BUSY);

        // Tune the remote radio like the backend profile
        MeshRadioProfile profile;
        loopbackGetRadioProfile(nullptr, RADIO_REGION_EU_868, RADIO_MODULATION_LORA, 0, &profile);
        REQUIRE_EQ(radio_set_modulation(&remote, profile.modulation), ERROR_NONE);
        radio_set_parameter(&remote, RADIO_PARAMETER_FREQUENCY, profile.frequency);
        radio_set_parameter(&remote, RADIO_PARAMETER_BANDWIDTH, profile.bandwidth);
        radio_set_parameter(&remote, RADIO_PARAMETER_SPREADING_FACTOR, profile.spreading_factor);
        radio_set_parameter(&remote, RADIO_PARAMETER_SYNC_WORD, profile.sync_word);
        REQUIRE_EQ(radio_set_enabled(&remote, true), ERROR_NONE);

        TaskEventGroup group {};
        task_event_group_construct(&group);
        static RadioRxEventSubscription remoteRx {};
        REQUIRE_EQ(radio_rx_event_subscribe(&remoteRx, &group, &remote), ERROR_NONE);
        MeshEventSubscription meshEvents {};
        REQUIRE_EQ(mesh_event_subscribe(&meshEvents, &group), ERROR_NONE);

        // Outgoing
        uint32_t seq = 0;
        REQUIRE_EQ(mesh_send_text(nullptr, 3, "hello mesh", &seq), ERROR_NONE);
        CHECK(waitFor([seq] { return getDelivery(seq) == MESH_DELIVERY_SENT; }));

        static RadioRxEvent rxEvent;
        REQUIRE_EQ(radio_rx_event_poll(&remoteRx, &rxEvent), ERROR_NONE);
        CHECK_EQ(rxEvent.data[0], 3);
        CHECK_EQ(std::string(reinterpret_cast<const char*>(rxEvent.data + 1), rxEvent.length - 1), "hello mesh");

        // Incoming
        const uint8_t reply[] = { 3, 'h', 'i', '!' };
        REQUIRE_EQ(radio_transmit(&remote, reply, sizeof(reply), nullptr), ERROR_NONE);
        MeshMessage history[4];
        size_t count = 0;
        CHECK(waitFor([&] {
            return mesh_history_get(0, history, 4, &count) == ERROR_NONE && count == 2;
        }));
        CHECK_FALSE(history[0].outgoing);
        CHECK_EQ(std::string(history[0].text), "hi!");
        CHECK_EQ(std::string(history[0].sender_name), "peer");
        CHECK_EQ(history[0].channel, 3);
        CHECK_EQ(history[0].delivery, MESH_DELIVERY_RECEIVED);
        CHECK(history[1].outgoing);

        MeshNode nodes[4];
        CHECK_EQ(mesh_get_nodes(nodes, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 1);
        CHECK_EQ(std::string(nodes[0].name), "peer");

        // Events: state, message, delivery and node events were queued
        MeshEvent event {};
        bool sawMessage = false;
        bool sawDelivery = false;
        while (mesh_event_poll(&meshEvents, &event) == ERROR_NONE) {
            sawMessage |= event.type == MESH_EVENT_MESSAGE;
            sawDelivery |= event.type == MESH_EVENT_DELIVERY;
        }
        CHECK(sawMessage);
        CHECK(sawDelivery);

        // Frames that aren't messages are only reported as events
        const uint8_t unreadable[] = { UNREADABLE_CHANNEL, 1, 2, 3, 4 };
        REQUIRE_EQ(radio_transmit(&remote, unreadable, sizeof(unreadable), nullptr), ERROR_NONE);
        const uint8_t advert[] = { ADVERT_CHANNEL };
        REQUIRE_EQ(radio_transmit(&remote, advert, sizeof(advert), nullptr), ERROR_NONE);
        bool sawUnreadable = false;
        bool sawAdvert = false;
        CHECK(waitFor([&] {
            while (mesh_event_poll(&meshEvents, &event) == ERROR_NONE) {
                if (event.type == MESH_EVENT_FRAME && event.frame.kind == MESH_FRAME_UNREADABLE) {
                    sawUnreadable = event.frame.length == sizeof(unreadable);
                }
                if (event.type == MESH_EVENT_FRAME && event.frame.kind == MESH_FRAME_ADVERT) {
                    sawAdvert = std::string(event.frame.name) == "advertiser" && event.frame.from.bytes[0] == 0x43;
                }
            }
            return sawUnreadable && sawAdvert;
        }));
        CHECK(mesh_history_get(0, history, 4, &count) == ERROR_NONE);
        CHECK_EQ(count, 2);

        // The sender of the incoming message was saved as a contact
        MeshContact contacts[4];
        REQUIRE_EQ(mesh_get_contacts(contacts, 4, &count), ERROR_NONE);
        REQUIRE_EQ(count, 1);
        CHECK_EQ(std::string(contacts[0].name), "peer");
        CHECK_EQ(std::string(contacts[0].backend), "loopback");

        // Contacts can be added, renamed and removed
        const MeshAddress alice = { .length = 1, .bytes = { 0x50 } };
        const MeshAddress unknown = { .length = 1, .bytes = { 0x51 } };
        const MeshAddress tooLong = { .length = 2, .bytes = { 0x52, 0x53 } };
        CHECK_EQ(mesh_add_contact(&tooLong, "bad"), ERROR_INVALID_ARGUMENT);
        CHECK_EQ(mesh_add_contact(&alice, "alice"), ERROR_NONE);
        CHECK_EQ(mesh_add_contact(&alice, nullptr), ERROR_NONE);
        CHECK_EQ(mesh_add_contact(&unknown, nullptr), ERROR_NONE);
        REQUIRE_EQ(mesh_get_contacts(contacts, 4, &count), ERROR_NONE);
        REQUIRE_EQ(count, 3);
        CHECK_EQ(std::string(contacts[1].name), "alice");
        // Without a name the address identifies the contact
        CHECK_EQ(std::string(contacts[2].name), "51");

        // Conversations only hold their own messages
        uint32_t directSeq = 0;
        uint32_t channelSeq = 0;
        REQUIRE_EQ(mesh_send_text(&alice, 0, "hi alice", &directSeq), ERROR_NONE);
        REQUIRE_EQ(mesh_send_text(nullptr, 5, "hello channel", &channelSeq), ERROR_NONE);
        MeshMessage conversation[4];
        CHECK_EQ(mesh_conversation_get(&alice, 0, 0, conversation, 4, &count), ERROR_NONE);
        REQUIRE_EQ(count, 1);
        CHECK_EQ(conversation[0].seq, directSeq);
        CHECK_EQ(std::string(conversation[0].text), "hi alice");
        CHECK_EQ(mesh_conversation_get(nullptr, 5, 0, conversation, 4, &count), ERROR_NONE);
        REQUIRE_EQ(count, 1);
        CHECK_EQ(conversation[0].seq, channelSeq);
        CHECK_EQ(mesh_conversation_get(nullptr, 6, 0, conversation, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 0);
        CHECK_EQ(mesh_conversation_get(&unknown, 0, 0, conversation, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 0);
        // The delivery state shows how far the message got
        CHECK(waitFor([directSeq] { return getDelivery(directSeq) == MESH_DELIVERY_SENT; }));

        // The URI of a saved contact and of this node come from the backend
        char uri[64];
        CHECK_EQ(mesh_get_contact_uri(&alice, uri, sizeof(uri)), ERROR_NONE);
        CHECK_EQ(std::string(uri), "loopback://alice/50");
        CHECK_EQ(mesh_get_contact_uri(nullptr, uri, sizeof(uri)), ERROR_NONE);
        CHECK_EQ(std::string(uri).substr(0, 11), "loopback://");
        CHECK_EQ(mesh_get_contact_uri(&unknown, uri, sizeof(uri)), ERROR_NONE);
        const MeshAddress stranger = { .length = 1, .bytes = { 0x77 } };
        CHECK_EQ(mesh_get_contact_uri(&stranger, uri, sizeof(uri)), ERROR_NOT_FOUND);

        CHECK_EQ(mesh_remove_contact(&alice), ERROR_NONE);
        CHECK_EQ(mesh_remove_contact(&alice), ERROR_NOT_FOUND);
        REQUIRE_EQ(mesh_get_contacts(contacts, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 2);
        // The messages remain
        CHECK_EQ(mesh_conversation_get(&alice, 0, 0, conversation, 4, &count), ERROR_NONE);
        CHECK_EQ(count, 1);

        // The loopback backend has no channels
        CHECK_EQ(mesh_remove_channel(0), ERROR_NOT_SUPPORTED);
        CHECK_EQ(mesh_remove_channel(2), ERROR_NOT_SUPPORTED);

        CHECK_EQ(mesh_cancel(seq), ERROR_INVALID_STATE);
        CHECK_EQ(mesh_raw_send("other", reply, sizeof(reply)), ERROR_INVALID_STATE);
        CHECK_EQ(mesh_raw_send("loopback", reply, sizeof(reply)), ERROR_NONE);

        CHECK_EQ(mesh_event_unsubscribe(&meshEvents), ERROR_NONE);
        CHECK_EQ(radio_rx_event_unsubscribe(&remoteRx), ERROR_NONE);
        task_event_group_destruct(&group);
    }

    CHECK_EQ(service_manager_stop(MESH_SERVICE_ID), ERROR_NONE);
    CHECK_EQ(mesh_get_state(), MESH_STATE_OFF);
    // Released on stop
    CHECK_EQ(radio_acquire(&local, "someone-else"), ERROR_NONE);
    CHECK_EQ(radio_release(&local), ERROR_NONE);

    CHECK_EQ(service_manager_remove(MESH_SERVICE_ID), ERROR_NONE);
    CHECK_EQ(mesh_backend_unregister(&loopbackApi), ERROR_NONE);
    radio_set_region(&local, RADIO_REGION_UNSET);
    radio_set_enabled(&remote, false);
    device_stop(&remote);
    device_stop(&local);
    device_remove(&remote);
    device_remove(&local);
    device_destruct(&remote);
    device_destruct(&local);
    file_system_remove(dataFs);
}

TEST_CASE("legacy mesh settings belong to the backend that owned them") {
    char dataPath[] = "/tmp/mesh_settings_test_XXXXXX";
    REQUIRE_NE(mkdtemp(dataPath), nullptr);
    FileSystem* dataFs = file_system_add("data", &fakeDataApi, dataPath);

    char directory[FILE_MAX_PATH_STRING_LENGTH];
    char path[FILE_MAX_PATH_STRING_LENGTH];
    REQUIRE_EQ(service_paths_get_user_data_directory(MESH_SERVICE_ID, directory, sizeof(directory)), ERROR_NONE);
    REQUIRE_EQ(service_paths_get_user_data_path(MESH_SERVICE_ID, "settings.properties", path, sizeof(path)), ERROR_NONE);
    REQUIRE_EQ(directory_make(directory, true), ERROR_NONE);
    PropertiesFile* file = properties_file_open(path);
    REQUIRE_NE(file, nullptr);
    properties_file_set(file, "preset", "Other");
    properties_file_set(file, "power", "7");
    REQUIRE_EQ(properties_file_close(file), ERROR_NONE);

    CHECK_EQ(mesh_settings_migrate_legacy("first"), ERROR_NONE);
    // A second call, for any backend, moves nothing
    CHECK_EQ(mesh_settings_migrate_legacy("second"), ERROR_NONE);

    MeshBackendSettings first = {};
    CHECK_EQ(mesh_settings_load_backend("first", &first), ERROR_NONE);
    CHECK_EQ(std::string(first.preset), "Other");
    CHECK_EQ(first.radio_settings.power, 7);

    MeshBackendSettings second = {};
    mesh_settings_load_backend("second", &second);
    CHECK_EQ(std::string(second.preset), "");
    CHECK_EQ(second.radio_settings.power, 0);

    file_system_remove(dataFs);
}

TEST_CASE("saving settings of two backends at the same time keeps both") {
    char dataPath[] = "/tmp/mesh_settings_test_XXXXXX";
    REQUIRE_NE(mkdtemp(dataPath), nullptr);
    FileSystem* dataFs = file_system_add("data", &fakeDataApi, dataPath);

    constexpr int ROUNDS = 40;
    const auto save = [](void* backend) -> int32_t {
        for (int i = 1; i <= ROUNDS; i++) {
            MeshBackendSettings settings = {};
            settings.radio_settings.power = i;
            if (mesh_settings_save_backend(static_cast<const char*>(backend), &settings) != ERROR_NONE) {
                return 1;
            }
        }
        return 0;
    };
    Thread* threads[2] = {
        thread_alloc_full("save_first", 8192, save, const_cast<char*>("first"), -1),
        thread_alloc_full("save_second", 8192, save, const_cast<char*>("second"), -1),
    };
    for (auto* thread : threads) {
        REQUIRE_EQ(thread_start(thread), ERROR_NONE);
    }
    for (auto* thread : threads) {
        CHECK_EQ(thread_join(thread, pdMS_TO_TICKS(10000), 1), ERROR_NONE);
        thread_free(thread);
    }

    // Both threads finished and neither one's keys were lost. The race that the file lock prevents is too narrow to fail reliably without it.
    MeshBackendSettings loaded = {};
    mesh_settings_load_backend("first", &loaded);
    CHECK_EQ(loaded.radio_settings.power, ROUNDS);
    mesh_settings_load_backend("second", &loaded);
    CHECK_EQ(loaded.radio_settings.power, ROUNDS);

    file_system_remove(dataFs);
}
