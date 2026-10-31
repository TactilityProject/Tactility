// SPDX-License-Identifier: GPL-3.0-only
#include <mesh_meshtastic/meshtastic_backend.h>
#include <mesh_meshtastic/meshtastic_protocol.h>

#include <mesh/backend.h>

#include <tactility/Memory.hpp>
#include <tactility/std/deque.hpp>
#include <tactility/std/map.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <utility>

#ifdef ESP_PLATFORM
#include <esp_random.h>
#else
#include <random>
#endif

constexpr auto* TAG = "meshtastic";

namespace {

constexpr size_t MAX_CHANNELS = 8;
constexpr size_t SEEN_PACKETS_LIMIT = 64;
constexpr size_t MAX_KNOWN_NAMES = 128;
constexpr uint64_t NODE_INFO_INTERVAL_MS = 3ULL * 3600 * 1000;
constexpr uint64_t ACK_TIMEOUT_MS = 60 * 1000;
constexpr auto* DEFAULT_CHANNEL_NAME = "LongFast";
constexpr int32_t MAX_RADIO_POWER = 22;

/** Meshtastic's modem presets. Index 0 is the default. */
struct ModemPreset {
    const char* name;
    int32_t bandwidth;
    int32_t spreadingFactor;
    int32_t codingRate;
};

constexpr std::array MODEM_PRESETS = {
    ModemPreset { .name = "LongFast", .bandwidth = 250000, .spreadingFactor = 11, .codingRate = 5 },
    ModemPreset { .name = "LongModerate", .bandwidth = 125000, .spreadingFactor = 11, .codingRate = 8 },
    ModemPreset { .name = "LongSlow", .bandwidth = 125000, .spreadingFactor = 12, .codingRate = 8 },
    ModemPreset { .name = "MediumFast", .bandwidth = 250000, .spreadingFactor = 9, .codingRate = 5 },
    ModemPreset { .name = "MediumSlow", .bandwidth = 250000, .spreadingFactor = 10, .codingRate = 5 },
    ModemPreset { .name = "ShortFast", .bandwidth = 250000, .spreadingFactor = 7, .codingRate = 5 },
    ModemPreset { .name = "ShortSlow", .bandwidth = 250000, .spreadingFactor = 8, .codingRate = 5 },
    ModemPreset { .name = "ShortTurbo", .bandwidth = 500000, .spreadingFactor = 7, .codingRate = 5 },
};

/** The names of MODEM_PRESETS and a terminating nullptr */
constexpr auto PRESET_NAMES = [] {
    std::array<const char*, MODEM_PRESETS.size() + 1> names {};
    for (size_t i = 0; i < MODEM_PRESETS.size(); i++) {
        names[i] = MODEM_PRESETS[i].name;
    }
    return names;
}();

/** Meshtastic's region table, used to place the channel's frequency slot */
struct RegionInfo {
    RadioRegion region;
    int32_t frequencyStart;
    int32_t frequencyEnd;
    int32_t powerLimit;
};

constexpr RegionInfo REGIONS[] = {
    { RADIO_REGION_US_915, 902000000, 928000000, 30 },
    { RADIO_REGION_EU_433, 433000000, 434000000, 12 },
    { RADIO_REGION_EU_868, 869400000, 869650000, 27 },
    { RADIO_REGION_AU_915, 915000000, 928000000, 30 },
    { RADIO_REGION_NZ_865, 864000000, 868000000, 36 },
    { RADIO_REGION_IN_865, 865000000, 867000000, 30 },
    { RADIO_REGION_RU_868, 868700000, 869200000, 20 },
    { RADIO_REGION_CN_470, 470000000, 510000000, 19 },
    { RADIO_REGION_JP_920, 920800000, 927800000, 16 },
};

struct Channel {
    uint8_t index;
    char name[MESH_NAME_MAX_LENGTH + 1];
    MeshtasticChannelKey key;
    uint8_t hash;
    /** The primary channel that was never customised is named after the selected preset, like other nodes name it */
    bool followsPreset;
};

struct Backend {
    MeshHost* host = nullptr;
    uint32_t nodeNum = 0;
    tt::std::vector<Channel> channels;
    tt::std::deque<std::pair<uint32_t, uint32_t>> seenPackets;
    tt::std::map<uint32_t, tt::std::string> names;
    /** Packet id of a sent direct message, to the message's sequence number and deadline */
    tt::std::map<uint32_t, std::pair<uint32_t, uint64_t>> pendingAcks;
    uint64_t lastNodeInfoMs = 0;
};

uint64_t getMillis() {
    return get_micros_since_boot() / 1000;
}

uint32_t randomU32() {
#ifdef ESP_PLATFORM
    return esp_random();
#else
    static std::random_device device;
    return device();
#endif
}

uint32_t newPacketId() {
    uint32_t id;
    do {
        id = randomU32();
    } while (id == 0);
    return id;
}

MeshAddress toAddress(uint32_t nodeNum) {
    MeshAddress address {};
    address.length = 4;
    for (int i = 0; i < 4; i++) {
        address.bytes[i] = (uint8_t)(nodeNum >> (8 * i));
    }
    return address;
}

// region Persistence

bool readFile(const char* path, tt::std::vector<uint8_t>& data) {
    FILE* file = fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    uint8_t buffer[256];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        data.insert(data.end(), buffer, buffer + read);
    }
    fclose(file);
    return true;
}

bool writeFile(const char* path, const uint8_t* data, size_t length) {
    FILE* file = fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const bool ok = fwrite(data, 1, length, file) == length;
    fclose(file);
    return ok;
}

void loadOrCreateNodeNum(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    const bool hasPath = mesh_host_get_data_path(backend->host, "node.bin", path, sizeof(path)) == ERROR_NONE;
    tt::std::vector<uint8_t> data;
    if (hasPath && readFile(path, data) && data.size() == 4) {
        backend->nodeNum = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
        return;
    }
    // Low numbers are reserved and the broadcast address is not a node
    do {
        backend->nodeNum = randomU32();
    } while (backend->nodeNum < 4 || backend->nodeNum == MESHTASTIC_BROADCAST);
    const uint8_t encoded[4] = { (uint8_t)backend->nodeNum, (uint8_t)(backend->nodeNum >> 8), (uint8_t)(backend->nodeNum >> 16), (uint8_t)(backend->nodeNum >> 24) };
    if (hasPath) {
        writeFile(path, encoded, sizeof(encoded));
    }
}

void addChannel(Backend* backend, uint8_t index, const char* name, const MeshtasticChannelKey& key, bool followsPreset = false) {
    Channel channel {};
    channel.index = index;
    strncpy(channel.name, name, MESH_NAME_MAX_LENGTH);
    channel.key = key;
    channel.hash = meshtastic_channel_hash(name, key);
    channel.followsPreset = followsPreset;
    std::erase_if(backend->channels, [index](const Channel& existing) { return existing.index == index; });
    backend->channels.push_back(channel);
    std::sort(backend->channels.begin(), backend->channels.end(), [](const Channel& a, const Channel& b) { return a.index < b.index; });
}

MeshtasticChannelKey getDefaultKey() {
    const uint8_t defaultPsk = 1;
    MeshtasticChannelKey key {};
    meshtastic_expand_psk(&defaultPsk, 1, &key);
    return key;
}

bool isDefaultKey(const MeshtasticChannelKey& key) {
    const MeshtasticChannelKey defaultKey = getDefaultKey();
    return key.length == defaultKey.length && memcmp(key.bytes, defaultKey.bytes, defaultKey.length) == 0;
}

// Record: index (1) + name (32) + key length (1) + key (32)
constexpr size_t CHANNEL_RECORD_SIZE = 1 + MESH_NAME_MAX_LENGTH + 1 + 1 + 32;

// The records keep the format of older versions, which can read the file back. A file next to it marks that the primary channel
// that was never customised is stored without a name. Earlier files stored it as "LongFast", which can't be told from one that was
// named that on purpose.
constexpr auto* CHANNELS_MARKER_FILE = "channels.preset";

void loadChannels(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    tt::std::vector<uint8_t> data;
    if (mesh_host_get_data_path(backend->host, "channels.bin", path, sizeof(path)) == ERROR_NONE && readFile(path, data)) {
        char markerPath[FILE_MAX_PATH_STRING_LENGTH];
        const bool hasMarker = mesh_host_get_data_path(backend->host, CHANNELS_MARKER_FILE, markerPath, sizeof(markerPath)) == ERROR_NONE &&
            access(markerPath, F_OK) == 0;
        for (size_t offset = 0; offset + CHANNEL_RECORD_SIZE <= data.size(); offset += CHANNEL_RECORD_SIZE) {
            char name[MESH_NAME_MAX_LENGTH + 1];
            memcpy(name, data.data() + offset + 1, sizeof(name));
            name[MESH_NAME_MAX_LENGTH] = '\0';
            MeshtasticChannelKey key {};
            key.length = data[offset + 1 + sizeof(name)];
            memcpy(key.bytes, data.data() + offset + 2 + sizeof(name), 32);
            if (key.length == 16 || key.length == 32) {
                // A primary channel without a name and with the default key is the one that follows the preset
                // An earlier file has "LongFast" for it, which is taken for the default because that is what nearly all of them are
                const bool legacyDefault = !hasMarker && strcmp(name, DEFAULT_CHANNEL_NAME) == 0;
                const bool followsPreset = data[offset] == 0 && (name[0] == '\0' || legacyDefault) && isDefaultKey(key);
                addChannel(backend, data[offset], followsPreset ? DEFAULT_CHANNEL_NAME : name, key, followsPreset);
            }
        }
    }
    if (backend->channels.empty()) {
        addChannel(backend, 0, DEFAULT_CHANNEL_NAME, getDefaultKey(), true);
    }
}

void saveChannels(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (mesh_host_get_data_path(backend->host, "channels.bin", path, sizeof(path)) != ERROR_NONE) {
        return;
    }
    tt::std::vector<uint8_t> data;
    for (const auto& channel : backend->channels) {
        data.push_back(channel.index);
        const char emptyName[MESH_NAME_MAX_LENGTH + 1] = {};
        const char* name = channel.followsPreset ? emptyName : channel.name;
        data.insert(data.end(), name, name + MESH_NAME_MAX_LENGTH + 1);
        data.push_back(channel.key.length);
        data.insert(data.end(), channel.key.bytes, channel.key.bytes + 32);
    }
    if (!writeFile(path, data.data(), data.size())) {
        return;
    }
    char markerPath[FILE_MAX_PATH_STRING_LENGTH];
    if (mesh_host_get_data_path(backend->host, CHANNELS_MARKER_FILE, markerPath, sizeof(markerPath)) == ERROR_NONE) {
        const uint8_t marker = 1;
        writeFile(markerPath, &marker, sizeof(marker));
    }
}

// endregion

const Channel* findChannel(Backend* backend, uint8_t index) {
    for (const auto& channel : backend->channels) {
        if (channel.index == index) {
            return &channel;
        }
    }
    return nullptr;
}

bool transmit(Backend* backend, const Channel& channel, MeshtasticHeader header, const MeshtasticData& data, uint32_t seq) {
    const tt::std::vector<uint8_t> plaintext = meshtastic_encode_data(data);
    if (plaintext.size() > MESHTASTIC_MAX_PAYLOAD_SIZE) {
        return false;
    }
    uint8_t frame[MESHTASTIC_HEADER_SIZE + MESHTASTIC_MAX_PAYLOAD_SIZE];
    header.from = backend->nodeNum;
    header.channelHash = channel.hash;
    header.relayNode = (uint8_t)backend->nodeNum;
    meshtastic_write_header(header, frame);
    if (!meshtastic_crypt(channel.key, header.id, header.from, plaintext.data(), frame + MESHTASTIC_HEADER_SIZE, plaintext.size())) {
        return false;
    }
    backend->seenPackets.emplace_back(header.from, header.id);
    while (backend->seenPackets.size() > SEEN_PACKETS_LIMIT) {
        backend->seenPackets.pop_front();
    }
    return mesh_host_transmit(backend->host, frame, MESHTASTIC_HEADER_SIZE + plaintext.size(), seq) == ERROR_NONE;
}

void sendNodeInfo(Backend* backend) {
    char name[MESH_NAME_MAX_LENGTH + 1];
    mesh_host_get_node_name(backend->host, name, sizeof(name));
    MeshtasticUser user;
    user.id = meshtastic_node_id(backend->nodeNum);
    user.longName = name;
    user.shortName = user.longName.substr(0, 4);

    MeshtasticData data;
    data.port = MeshtasticPort::NodeInfo;
    data.payload = meshtastic_encode_user(user);

    MeshtasticHeader header;
    header.to = MESHTASTIC_BROADCAST;
    header.id = newPacketId();
    const Channel* primary = findChannel(backend, 0);
    if (primary != nullptr && transmit(backend, *primary, header, data, 0)) {
        LOG_I(TAG, "NodeInfo sent as %s \"%s\"", user.id.c_str(), name);
    }
    backend->lastNodeInfoMs = getMillis();
}

tt::std::string getSenderName(Backend* backend, uint32_t nodeNum) {
    const auto iterator = backend->names.find(nodeNum);
    return (iterator != backend->names.end()) ? iterator->second : meshtastic_node_id(nodeNum);
}

void sendRoutingAck(Backend* backend, const Channel& channel, uint32_t to, uint32_t requestId) {
    MeshtasticData data;
    data.port = MeshtasticPort::Routing;
    data.payload = meshtastic_encode_routing_ack();
    data.requestId = requestId;
    MeshtasticHeader header;
    header.to = to;
    header.id = newPacketId();
    transmit(backend, channel, header, data, 0);
}

/** Routing.error_reason, field 3 */
uint32_t getRoutingError(const tt::std::vector<uint8_t>& payload) {
    if (payload.size() >= 2 && payload[0] == 0x18) {
        return payload[1] & 0x7F;
    }
    return 0;
}

void onData(Backend* backend, const Channel& channel, const MeshtasticHeader& header, const MeshtasticData& data, float rssi, float snr) {
    const MeshAddress from = toAddress(header.from);
    const bool toUs = header.to == backend->nodeNum;

    switch (data.port) {
        case MeshtasticPort::TextMessage: {
            if (!toUs && header.to != MESHTASTIC_BROADCAST) {
                return;
            }
            if (toUs && header.wantAck) {
                sendRoutingAck(backend, channel, header.from, header.id);
            }
            const tt::std::string text(data.payload.begin(), data.payload.end());
            const tt::std::string sender = getSenderName(backend, header.from);
            // Channel messages have no peer, so they group by channel instead of by sender
            mesh_host_receive_text(backend->host, toUs ? &from : nullptr, channel.index, sender.c_str(), text.c_str(), rssi, snr, 0);
            break;
        }
        case MeshtasticPort::NodeInfo: {
            MeshtasticUser user;
            if (meshtastic_decode_user(data.payload.data(), data.payload.size(), &user) && !user.longName.empty()) {
                if (backend->names.size() >= MAX_KNOWN_NAMES && !backend->names.contains(header.from)) {
                    backend->names.erase(backend->names.begin());
                }
                backend->names[header.from] = user.longName;
                mesh_host_advert_heard(backend->host, &from, user.longName.c_str(), rssi, snr);
            }
            break;
        }
        case MeshtasticPort::Routing: {
            const auto iterator = backend->pendingAcks.find(data.requestId);
            if (toUs && iterator != backend->pendingAcks.end()) {
                const uint32_t error = getRoutingError(data.payload);
                mesh_host_set_delivery(backend->host, iterator->second.first, error == 0 ? MESH_DELIVERY_DELIVERED : MESH_DELIVERY_FAILED);
                backend->pendingAcks.erase(iterator);
            }
            break;
        }
        default:
            break;
    }
}

// region MeshBackendApi

void* create(MeshHost* host) {
    auto* backend = tt::newObject<Backend>();
    if (backend == nullptr) {
        return nullptr;
    }
    backend->host = host;
    loadOrCreateNodeNum(backend);
    loadChannels(backend);
    LOG_I(TAG, "Node %s", meshtastic_node_id(backend->nodeNum).c_str());
    return backend;
}

void destroy(void* context) {
    tt::deleteObject(static_cast<Backend*>(context));
}

/** The primary channel that was never customised takes the name of the selected preset. It isn't saved, so a channel that was configured with a preset's name keeps its name. */
void followPreset(Backend* backend, const char* presetName) {
    for (auto& channel : backend->channels) {
        if (channel.index == 0 && channel.followsPreset && strcmp(channel.name, presetName) != 0) {
            // Copied because addChannel() replaces the channel
            const MeshtasticChannelKey key = channel.key;
            addChannel(backend, 0, presetName, key, true);
            return;
        }
    }
}

error_t getRadioProfile(void* context, RadioRegion region, RadioModulation modulation, size_t presetIndex, MeshRadioProfile* profile) {
    auto* backend = static_cast<Backend*>(context);
    if (modulation != RADIO_MODULATION_LORA || presetIndex >= MODEM_PRESETS.size()) {
        return ERROR_NOT_SUPPORTED;
    }
    const ModemPreset& preset = MODEM_PRESETS[presetIndex];
    for (const auto& info : REGIONS) {
        if (info.region != region) {
            continue;
        }
        if (preset.bandwidth > info.frequencyEnd - info.frequencyStart) {
            return ERROR_NOT_SUPPORTED;
        }
        // Without a backend there are no channels, and the preset's own name selects the slot
        if (backend != nullptr) {
            followPreset(backend, preset.name);
        }
        // The slot that the primary channel's name selects
        const Channel* primary = (backend != nullptr) ? findChannel(backend, 0) : nullptr;
        const char* slotName = (primary != nullptr && primary->name[0] != '\0') ? primary->name : preset.name;
        *profile = {
            .modulation = RADIO_MODULATION_LORA,
            .frequency = meshtastic_channel_frequency(slotName, info.frequencyStart, info.frequencyEnd, preset.bandwidth),
            .bandwidth = preset.bandwidth,
            .spreading_factor = preset.spreadingFactor,
            .coding_rate = preset.codingRate,
            .sync_word = 0x2B,
            .preamble_length = 16,
            .power = std::min(info.powerLimit, MAX_RADIO_POWER),
        };
        return ERROR_NONE;
    }
    return ERROR_NOT_SUPPORTED;
}

void onStart(void* context) {
    sendNodeInfo(static_cast<Backend*>(context));
}

bool isValidContactAddress(void*, const MeshAddress* address) {
    return address->length == 4;
}

void onNodeNameChanged(void* context) {
    sendNodeInfo(static_cast<Backend*>(context));
}

bool onReceive(void* context, const uint8_t* frame, size_t length, float rssi, float snr) {
    auto* backend = static_cast<Backend*>(context);
    MeshtasticHeader header;
    if (!meshtastic_read_header(frame, length, &header) || length == MESHTASTIC_HEADER_SIZE ||
        length - MESHTASTIC_HEADER_SIZE > MESHTASTIC_MAX_PAYLOAD_SIZE) {
        return false;
    }
    if (header.from == backend->nodeNum) {
        return true;
    }

    const auto key = std::make_pair(header.from, header.id);
    if (std::find(backend->seenPackets.begin(), backend->seenPackets.end(), key) != backend->seenPackets.end()) {
        return true;
    }
    backend->seenPackets.push_back(key);
    while (backend->seenPackets.size() > SEEN_PACKETS_LIMIT) {
        backend->seenPackets.pop_front();
    }

    const MeshAddress from = toAddress(header.from);
    mesh_host_node_heard(backend->host, &from, nullptr, rssi, snr);

    // The 1-byte hash can match several channels: the one that decodes to a valid Data message wins
    const size_t payloadLength = length - MESHTASTIC_HEADER_SIZE;
    uint8_t plaintext[MESHTASTIC_MAX_PAYLOAD_SIZE];
    for (const auto& channel : backend->channels) {
        if (channel.hash != header.channelHash) {
            continue;
        }
        MeshtasticData data;
        if (meshtastic_crypt(channel.key, header.id, header.from, frame + MESHTASTIC_HEADER_SIZE, plaintext, payloadLength) &&
            meshtastic_decode_data(plaintext, payloadLength, &data)) {
            onData(backend, channel, header, data, rssi, snr);
            return true;
        }
    }
    return false;
}

void onTick(void* context) {
    auto* backend = static_cast<Backend*>(context);
    const uint64_t nowMs = getMillis();
    if (nowMs - backend->lastNodeInfoMs >= NODE_INFO_INTERVAL_MS) {
        sendNodeInfo(backend);
    }
    for (auto iterator = backend->pendingAcks.begin(); iterator != backend->pendingAcks.end();) {
        if (nowMs >= iterator->second.second) {
            mesh_host_set_delivery(backend->host, iterator->second.first, MESH_DELIVERY_FAILED);
            iterator = backend->pendingAcks.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

error_t sendText(void* context, uint32_t seq, const MeshAddress* to, uint8_t channelIndex, const char* text) {
    auto* backend = static_cast<Backend*>(context);

    MeshtasticData data;
    data.port = MeshtasticPort::TextMessage;
    data.payload.assign(text, text + strlen(text));

    MeshtasticHeader header;
    header.id = newPacketId();
    const Channel* channel;
    if (to == nullptr) {
        header.to = MESHTASTIC_BROADCAST;
        channel = findChannel(backend, channelIndex);
    } else {
        if (to->length != 4) {
            return ERROR_INVALID_ARGUMENT;
        }
        header.to = to->bytes[0] | (to->bytes[1] << 8) | (to->bytes[2] << 16) | ((uint32_t)to->bytes[3] << 24);
        header.wantAck = true;
        channel = findChannel(backend, 0);
    }
    if (channel == nullptr) {
        return ERROR_NOT_FOUND;
    }
    if (!transmit(backend, *channel, header, data, seq)) {
        return ERROR_RESOURCE;
    }
    if (header.wantAck) {
        backend->pendingAcks[header.id] = { seq, getMillis() + ACK_TIMEOUT_MS };
    }
    return ERROR_NONE;
}

error_t getChannels(void* context, MeshChannel* channels, size_t maxCount, size_t* count) {
    auto* backend = static_cast<Backend*>(context);
    *count = 0;
    for (const auto& channel : backend->channels) {
        if (*count >= maxCount) {
            break;
        }
        MeshChannel& out = channels[(*count)++];
        out = {};
        out.index = channel.index;
        strncpy(out.name, channel.name, MESH_NAME_MAX_LENGTH);
        memcpy(out.key, channel.key.bytes, channel.key.length);
        out.key_length = channel.key.length;
    }
    return ERROR_NONE;
}

error_t setChannel(void* context, const MeshChannel* channel) {
    auto* backend = static_cast<Backend*>(context);
    MeshtasticChannelKey key {};
    if (!meshtastic_expand_psk(channel->key, channel->key_length, &key)) {
        return ERROR_INVALID_ARGUMENT;
    }
    if (findChannel(backend, channel->index) == nullptr && backend->channels.size() >= MAX_CHANNELS) {
        return ERROR_RESOURCE;
    }
    addChannel(backend, channel->index, channel->name, key);
    saveChannels(backend);
    return ERROR_NONE;
}

error_t removeChannel(void* context, uint8_t index) {
    auto* backend = static_cast<Backend*>(context);
    // Meshtastic needs the primary channel to place the node in a frequency slot
    if (index == 0) {
        return ERROR_INVALID_ARGUMENT;
    }
    const size_t removed = std::erase_if(backend->channels, [index](const Channel& existing) { return existing.index == index; });
    if (removed == 0) {
        return ERROR_NOT_FOUND;
    }
    saveChannels(backend);
    return ERROR_NONE;
}

// endregion

} // namespace

const MeshBackendApi meshtastic_backend_api = {
    .id = "meshtastic",
    .create = create,
    .destroy = destroy,
    .presets = PRESET_NAMES.data(),
    .get_radio_profile = getRadioProfile,
    .on_start = onStart,
    .on_receive = onReceive,
    .on_tick = onTick,
    .send_text = sendText,
    .get_channels = getChannels,
    .set_channel = setChannel,
    .is_valid_contact_address = isValidContactAddress,
    .on_node_name_changed = onNodeNameChanged,
    .remove_channel = removeChannel,
    .features = MESH_FEATURE_KEYED_CHANNELS,
};
