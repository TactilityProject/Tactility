// SPDX-License-Identifier: Apache-2.0
#include <mesh_meshcore/meshcore_backend.h>
#include <mesh_meshcore/meshcore_protocol.h>

#include <mesh/backend.h>

#include <tactility/Memory.hpp>
#include <tactility/std/deque.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <tactility/log.h>
#include <tactility/paths.h>
#include <tactility/time.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>

constexpr auto* TAG = "meshcore";

namespace {

constexpr size_t MAX_CONTACTS = 64;
constexpr size_t MAX_CHANNELS = 8;
constexpr size_t SEEN_PACKETS_LIMIT = 64;
constexpr uint64_t ADVERT_INTERVAL_MS = 12ULL * 3600 * 1000;
/** How long to wait before a flooded advert that failed is tried again */
constexpr uint64_t ADVERT_RETRY_MS = 60 * 1000;
/** How long to wait for the acknowledgement of one attempt */
constexpr uint64_t ACK_TIMEOUT_MS = 20 * 1000;
/** The attempt number of a message is 2 bits wide, so there are 4 attempts */
constexpr uint8_t MAX_RETRIES = 3;
/** The deadline of a message whose frame hasn't been transmitted yet. The wait for the ACK starts once it was. */
constexpr uint64_t WAITING_FOR_TRANSMISSION = UINT64_MAX;
/** The attempts over a known path come first, and flooding takes over for the others */
constexpr uint8_t PATH_ATTEMPTS = 2;
constexpr size_t RECENT_TEXTS_LIMIT = 32;
constexpr size_t CHANNEL_SENDS_LIMIT = 8;
constexpr uint8_t TEXT_TYPE_PLAIN = 0;
constexpr int64_t MIN_VALID_WALL_TIME = 1700000000;
constexpr size_t FRAME_SIZE = 255;

struct Contact {
    uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE];
    char name[MESH_NAME_MAX_LENGTH + 1];
};

struct Channel {
    uint8_t index;
    char name[MESH_NAME_MAX_LENGTH + 1];
    uint8_t key[16];
    MeshCoreChannel channel;
};

/** A channel message of this node. Channel messages aren't acknowledged, so repeaters sending it on again is the only sign that it got out. */
struct ChannelSend {
    uint32_t seq;
    uint32_t packetHash;
    uint8_t repeats;
};

/** The repeaters between this node and a contact, in the order of sending to the contact */
struct KnownPath {
    uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE];
    uint8_t length;
    uint8_t hashSize;
    uint8_t hops[MESHCORE_MAX_PATH_SIZE];
};

/** A direct message that waits for an acknowledgement. Every attempt has its own acknowledgement hash. */
struct PendingSend {
    uint32_t seq;
    uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE];
    uint32_t timestamp;
    tt::std::string text;
    /** The attempts below this number go over @a path */
    uint8_t pathAttempts;
    KnownPath path;
    /** 0 for the first transmission */
    uint8_t attempt;
    uint32_t ackHashes[MAX_RETRIES + 1];
    uint64_t deadlineMs;
};

constexpr auto* PUBLIC_CHANNEL_NAME = "Public";

struct Backend {
    MeshHost* host = nullptr;
    MeshCoreIdentity identity {};
    tt::std::vector<Contact> contacts;
    tt::std::vector<Channel> channels;
    tt::std::deque<uint32_t> seenPackets;
    tt::std::vector<PendingSend> pendingSends;
    /** Paths that an acknowledgement came back over. They are lost on restart. */
    tt::std::vector<KnownPath> knownPaths;
    tt::std::deque<ChannelSend> channelSends;
    /** Timestamp and sender of received direct messages, to drop the retries of a message that arrived already */
    tt::std::deque<uint64_t> recentTexts;
    uint64_t lastAdvertMs = 0;
    /** Wall time of the last flooded advert, 0 when unknown */
    uint32_t lastFloodAdvertTime = 0;
    /** The advert that follows a start waits for a valid clock, to see whether the last flooded one is recent enough */
    bool floodAdvertPending = false;
    /** A failed flooded advert isn't tried again before this uptime */
    uint64_t nextFloodAdvertMs = 0;
};

uint64_t getMillis() {
    return get_micros_since_boot() / 1000;
}

bool isWallTimeValid() {
    return static_cast<int64_t>(time(nullptr)) >= MIN_VALID_WALL_TIME;
}

/** MeshCore uses seconds since the epoch, or uptime when the clock was never set */
uint32_t getTimestamp() {
    const int64_t now = (int64_t)time(nullptr);
    return (now >= MIN_VALID_WALL_TIME) ? (uint32_t)now : (uint32_t)(getMillis() / 1000);
}

// region Persistence

bool getPath(Backend* backend, const char* name, char* path, size_t size) {
    return mesh_host_get_data_path(backend->host, name, path, size) == ERROR_NONE;
}

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

/**
 * Writes the new contents to "<path>.tmp" and checks that they are complete before they replace the file.
 * The previous file is kept until the new one is in place, because some filesystems can't rename over an existing file.
 */
bool writeFileSafely(const char* path, const uint8_t* data, size_t length) {
    char tempPath[FILE_MAX_PATH_STRING_LENGTH];
    char backupPath[FILE_MAX_PATH_STRING_LENGTH];
    if (snprintf(tempPath, sizeof(tempPath), "%s.tmp", path) >= (int)sizeof(tempPath) ||
        snprintf(backupPath, sizeof(backupPath), "%s.old", path) >= (int)sizeof(backupPath)) {
        return false;
    }
    FILE* file = fopen(tempPath, "wb");
    if (file == nullptr) {
        return false;
    }
    const bool written = (length == 0 || fwrite(data, 1, length, file) == length) && fflush(file) == 0;
    const bool closed = fclose(file) == 0;
    if (!written || !closed) {
        remove(tempPath);
        return false;
    }
    if (rename(tempPath, path) == 0) {
        return true;
    }
    // The file exists and can't be replaced by a rename: move it aside, so that it can be restored
    remove(backupPath);
    if (rename(path, backupPath) != 0) {
        remove(tempPath);
        return false;
    }
    if (rename(tempPath, path) != 0) {
        rename(backupPath, path);
        remove(tempPath);
        return false;
    }
    remove(backupPath);
    return true;
}

void loadOrCreateIdentity(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getPath(backend, "identity.bin", path, sizeof(path))) {
        meshcore_identity_create(&backend->identity);
        return;
    }
    tt::std::vector<uint8_t> data;
    if (readFile(path, data) && data.size() == sizeof(MeshCoreIdentity)) {
        memcpy(&backend->identity, data.data(), sizeof(MeshCoreIdentity));
        return;
    }
    meshcore_identity_create(&backend->identity);
    if (!writeFile(path, reinterpret_cast<const uint8_t*>(&backend->identity), sizeof(MeshCoreIdentity))) {
        LOG_E(TAG, "Failed to save the identity");
    }
}

void loadContacts(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    tt::std::vector<uint8_t> data;
    if (!getPath(backend, "contacts.bin", path, sizeof(path)) || !readFile(path, data)) {
        return;
    }
    for (size_t offset = 0; offset + sizeof(Contact) <= data.size() && backend->contacts.size() < MAX_CONTACTS; offset += sizeof(Contact)) {
        Contact contact;
        memcpy(&contact, data.data() + offset, sizeof(Contact));
        contact.name[MESH_NAME_MAX_LENGTH] = '\0';
        backend->contacts.push_back(contact);
    }
}

void saveContacts(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (getPath(backend, "contacts.bin", path, sizeof(path))) {
        writeFile(path, reinterpret_cast<const uint8_t*>(backend->contacts.data()), backend->contacts.size() * sizeof(Contact));
    }
}

void loadLastFloodAdvertTime(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    tt::std::vector<uint8_t> data;
    backend->lastFloodAdvertTime = 0;
    if (getPath(backend, "advert_time.bin", path, sizeof(path)) && readFile(path, data) && data.size() == sizeof(uint32_t)) {
        memcpy(&backend->lastFloodAdvertTime, data.data(), sizeof(uint32_t));
    }
}

void saveLastFloodAdvertTime(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (getPath(backend, "advert_time.bin", path, sizeof(path))) {
        writeFile(path, reinterpret_cast<const uint8_t*>(&backend->lastFloodAdvertTime), sizeof(uint32_t));
    }
}

void addChannel(Backend* backend, uint8_t index, const char* name, const uint8_t key[16]) {
    Channel channel {};
    channel.index = index;
    strncpy(channel.name, name, MESH_NAME_MAX_LENGTH);
    memcpy(channel.key, key, 16);
    meshcore_channel_from_key(key, &channel.channel);
    std::erase_if(backend->channels, [index](const Channel& existing) { return existing.index == index; });
    backend->channels.push_back(channel);
    std::sort(backend->channels.begin(), backend->channels.end(), [](const Channel& a, const Channel& b) { return a.index < b.index; });
}

// File: records of index (1) + name (32) + key (16). The format is the one of older versions, which can read a file back.
// A save replaces the file as a whole, so a file that is whole records is complete and an empty one is an empty list.
constexpr size_t CHANNEL_RECORD_SIZE = 1 + MESH_NAME_MAX_LENGTH + 1 + 16;

/**
 * Adds the channels of a saved file.
 * @return false when the file ends in a partial record, in which case no channel was added
 */
bool parseChannels(Backend* backend, const tt::std::vector<uint8_t>& data) {
    if (data.size() % CHANNEL_RECORD_SIZE != 0) {
        return false;
    }
    for (size_t offset = 0; offset < data.size(); offset += CHANNEL_RECORD_SIZE) {
        char name[MESH_NAME_MAX_LENGTH + 1];
        memcpy(name, data.data() + offset + 1, sizeof(name));
        name[MESH_NAME_MAX_LENGTH] = '\0';
        addChannel(backend, data[offset], name, data.data() + offset + 1 + sizeof(name));
    }
    return true;
}

void loadChannels(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    char backupPath[FILE_MAX_PATH_STRING_LENGTH];
    // Only a node without a complete saved list starts with the Public channel, so that it can be removed
    bool saved = false;
    if (getPath(backend, "channels.bin", path, sizeof(path)) && snprintf(backupPath, sizeof(backupPath), "%s.old", path) < (int)sizeof(backupPath)) {
        // The backup is the previous file when a save was interrupted while it replaced the file
        for (const char* candidate : { static_cast<const char*>(path), static_cast<const char*>(backupPath) }) {
            tt::std::vector<uint8_t> data;
            if (readFile(candidate, data) && parseChannels(backend, data)) {
                saved = true;
                break;
            }
        }
    }
    if (!saved) {
        addChannel(backend, 0, PUBLIC_CHANNEL_NAME, MESHCORE_PUBLIC_CHANNEL_KEY);
    }
}

void saveChannels(Backend* backend) {
    char path[FILE_MAX_PATH_STRING_LENGTH];
    if (!getPath(backend, "channels.bin", path, sizeof(path))) {
        return;
    }
    tt::std::vector<uint8_t> data;
    for (const auto& channel : backend->channels) {
        data.push_back(channel.index);
        data.insert(data.end(), channel.name, channel.name + MESH_NAME_MAX_LENGTH + 1);
        data.insert(data.end(), channel.key, channel.key + 16);
    }
    if (!writeFileSafely(path, data.data(), data.size())) {
        LOG_E(TAG, "Failed to save the channels");
    }
}

// endregion

MeshAddress toAddress(const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE]) {
    MeshAddress address {};
    address.length = MESHCORE_PUBLIC_KEY_SIZE;
    memcpy(address.bytes, publicKey, MESHCORE_PUBLIC_KEY_SIZE);
    return address;
}

Contact* findContact(Backend* backend, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE]) {
    for (auto& contact : backend->contacts) {
        if (memcmp(contact.publicKey, publicKey, MESHCORE_PUBLIC_KEY_SIZE) == 0) {
            return &contact;
        }
    }
    return nullptr;
}

/**
 * Flooded packets get the path hash size of the network, and a code that limits them to the region scope.
 * @return false when the scope's code couldn't be computed, because sending the packet without it would leave the scope
 */
bool applyNetwork(Backend* backend, const MeshCorePacket& original, MeshCorePacket* result) {
    MeshCorePacket packet = original;
    if (packet.route != MeshCoreRoute::Flood) {
        *result = packet;
        return true;
    }
    MeshNetworkSettings network;
    mesh_host_get_network(backend->host, &network);
    packet.pathHashSize = static_cast<uint8_t>(std::clamp<int32_t>(network.path_hash_size, 1, MESH_PATH_HASH_SIZE_MAX));
    if (network.region_scope[0] != '\0') {
        uint8_t key[16];
        meshcore_scope_key(network.region_scope, key);
        uint16_t code;
        if (!meshcore_transport_code(key, packet, &code)) {
            return false;
        }
        packet.route = MeshCoreRoute::TransportFlood;
        packet.transportCodes[0] = code;
        packet.transportCodes[1] = 0;
    }
    *result = packet;
    return true;
}

bool transmit(Backend* backend, const MeshCorePacket& original, uint32_t seq) {
    MeshCorePacket packet;
    if (!applyNetwork(backend, original, &packet)) {
        return false;
    }
    uint8_t frame[FRAME_SIZE];
    const size_t length = meshcore_packet_serialize(packet, frame, sizeof(frame));
    if (length == 0) {
        return false;
    }
    // Our own packets come back when repeated, so remember them as seen
    backend->seenPackets.push_back(meshcore_packet_hash(packet));
    while (backend->seenPackets.size() > SEEN_PACKETS_LIMIT) {
        backend->seenPackets.pop_front();
    }
    return mesh_host_transmit(backend->host, frame, length, seq) == ERROR_NONE;
}

/** A zero hop advert is a direct packet without a path, so repeaters don't send it on. @return false when it couldn't be sent */
bool sendAdvert(Backend* backend, MeshCoreRoute route = MeshCoreRoute::Flood) {
    char name[MESH_NAME_MAX_LENGTH + 1];
    mesh_host_get_node_name(backend->host, name, sizeof(name));
    MeshCorePacket packet;
    bool sent = false;
    if (meshcore_build_advert(backend->identity, getTimestamp(), name, &packet)) {
        packet.route = route;
        sent = transmit(backend, packet, 0);
    }
    if (sent) {
        LOG_I(TAG, "Advert sent as \"%s\"", name);
    }
    if (route == MeshCoreRoute::Flood) {
        if (!sent) {
            backend->nextFloodAdvertMs = getMillis() + ADVERT_RETRY_MS;
        } else {
            backend->lastAdvertMs = getMillis();
            if (isWallTimeValid()) {
                backend->lastFloodAdvertTime = static_cast<uint32_t>(time(nullptr));
                saveLastFloodAdvertTime(backend);
            }
        }
    }
    return sent;
}

// region Receiving

bool onAdvert(Backend* backend, const MeshCorePacket& packet, float rssi, float snr) {
    MeshCoreAdvert advert;
    if (!meshcore_parse_advert(packet, &advert)) {
        LOG_D(TAG, "Dropped advert with an invalid signature");
        return false;
    }
    if (memcmp(advert.publicKey, backend->identity.publicKey, MESHCORE_PUBLIC_KEY_SIZE) == 0) {
        return true;
    }

    auto* contact = findContact(backend, advert.publicKey);
    if (contact == nullptr) {
        if (backend->contacts.size() >= MAX_CONTACTS) {
            backend->contacts.erase(backend->contacts.begin());
        }
        backend->contacts.push_back({});
        contact = &backend->contacts.back();
        memcpy(contact->publicKey, advert.publicKey, MESHCORE_PUBLIC_KEY_SIZE);
    }
    strncpy(contact->name, advert.name.c_str(), MESH_NAME_MAX_LENGTH);
    contact->name[MESH_NAME_MAX_LENGTH] = '\0';
    saveContacts(backend);

    const MeshAddress address = toAddress(advert.publicKey);
    mesh_host_advert_heard(backend->host, &address, contact->name, rssi, snr);
    return true;
}

/** @return false when no known channel decrypts the message */
bool onGroupText(Backend* backend, const MeshCorePacket& packet, float rssi, float snr) {
    for (const auto& channel : backend->channels) {
        MeshCoreText text;
        if (!meshcore_parse_group_text(channel.channel, packet, &text)) {
            continue;
        }
        if ((text.flags >> 2) != TEXT_TYPE_PLAIN) {
            return true;
        }
        // Channel messages carry the sender's name as a "name: " prefix
        tt::std::string sender;
        tt::std::string body = text.text;
        const size_t separator = text.text.find(": ");
        if (separator != tt::std::string::npos) {
            sender = text.text.substr(0, separator);
            body = text.text.substr(separator + 2);
        }
        mesh_host_receive_text(backend->host, nullptr, channel.index, sender.c_str(), body.c_str(), rssi, snr, text.timestamp);
        return true;
    }
    return false;
}

/** Identifies a message by its time, sender and text, and not by its attempt number, so that its retries get the same id */
uint64_t getTextId(uint32_t timestamp, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE], const tt::std::string& text) {
    uint64_t hash = 14695981039346656037ULL;
    const auto add = [&hash](uint8_t byte) { hash = (hash ^ byte) * 1099511628211ULL; };
    for (size_t i = 0; i < sizeof(timestamp); i++) {
        add(static_cast<uint8_t>(timestamp >> (8 * i)));
    }
    for (size_t i = 0; i < MESHCORE_PUBLIC_KEY_SIZE; i++) {
        add(publicKey[i]);
    }
    for (char character : text) {
        add(static_cast<uint8_t>(character));
    }
    return hash;
}

/** @return false when the message isn't for this node or its sender is unknown */
bool onTextMessage(Backend* backend, const MeshCorePacket& packet, float rssi, float snr) {
    uint8_t destinationHash;
    uint8_t sourceHash;
    if (!meshcore_peek_text_message(packet, &destinationHash, &sourceHash) || destinationHash != backend->identity.publicKey[0]) {
        return false;
    }

    // Hashes are 1 byte, so several contacts may match: the MAC tells which one sent it
    for (const auto& contact : backend->contacts) {
        if (contact.publicKey[0] != sourceHash) {
            continue;
        }
        MeshCoreText text;
        uint32_t ackHash;
        if (!meshcore_parse_text_message(backend->identity, contact.publicKey, packet, &text, &ackHash)) {
            continue;
        }

        MeshCorePacket ack;
        meshcore_build_ack(ackHash, &ack);
        transmit(backend, ack, 0);

        const uint64_t textId = getTextId(text.timestamp, contact.publicKey, text.text);
        if (std::find(backend->recentTexts.begin(), backend->recentTexts.end(), textId) != backend->recentTexts.end()) {
            return true;
        }
        backend->recentTexts.push_back(textId);
        while (backend->recentTexts.size() > RECENT_TEXTS_LIMIT) {
            backend->recentTexts.pop_front();
        }

        if ((text.flags >> 2) == TEXT_TYPE_PLAIN) {
            const MeshAddress address = toAddress(contact.publicKey);
            mesh_host_node_heard(backend->host, &address, nullptr, rssi, snr);
            mesh_host_receive_text(backend->host, &address, 0, contact.name, text.text.c_str(), rssi, snr, text.timestamp);
        }
        return true;
    }
    LOG_D(TAG, "Text message from an unknown sender (hash %02X)", sourceHash);
    return false;
}

const KnownPath* findKnownPath(Backend* backend, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE]) {
    for (const auto& path : backend->knownPaths) {
        if (memcmp(path.publicKey, publicKey, MESHCORE_PUBLIC_KEY_SIZE) == 0) {
            return &path;
        }
    }
    return nullptr;
}

void forgetPath(Backend* backend, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE]) {
    std::erase_if(backend->knownPaths, [publicKey](const KnownPath& path) {
        return memcmp(path.publicKey, publicKey, MESHCORE_PUBLIC_KEY_SIZE) == 0;
    });
}

/** The path of a flooded acknowledgement lists the repeaters from the contact to this node, so it is stored reversed */
void learnPath(Backend* backend, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE], const MeshCorePacket& ack) {
    if (ack.route != MeshCoreRoute::Flood && ack.route != MeshCoreRoute::TransportFlood) {
        return;
    }
    KnownPath path {};
    memcpy(path.publicKey, publicKey, MESHCORE_PUBLIC_KEY_SIZE);
    path.length = ack.pathLength;
    path.hashSize = ack.pathHashSize;
    for (uint8_t hop = 0; hop < ack.pathLength; hop++) {
        memcpy(path.hops + hop * ack.pathHashSize, ack.path + (ack.pathLength - 1 - hop) * ack.pathHashSize, ack.pathHashSize);
    }
    forgetPath(backend, publicKey);
    if (backend->knownPaths.size() >= MAX_CONTACTS) {
        backend->knownPaths.erase(backend->knownPaths.begin());
    }
    backend->knownPaths.push_back(path);
}

/** Marks the message that @a ackHash belongs to as delivered */
void completeSend(Backend* backend, uint32_t ackHash, const MeshCorePacket* ack, const KnownPath* path) {    // An acknowledgement of an earlier attempt that arrives late counts too
    for (auto iterator = backend->pendingSends.begin(); iterator != backend->pendingSends.end(); ++iterator) {
        if (std::find(iterator->ackHashes, iterator->ackHashes + iterator->attempt + 1, ackHash) != iterator->ackHashes + iterator->attempt + 1) {
            mesh_host_set_delivery(backend->host, iterator->seq, MESH_DELIVERY_DELIVERED);
            if (path != nullptr) {
                forgetPath(backend, iterator->publicKey);
                backend->knownPaths.push_back(*path);
                memcpy(backend->knownPaths.back().publicKey, iterator->publicKey, MESHCORE_PUBLIC_KEY_SIZE);
            } else if (ack != nullptr) {
                learnPath(backend, iterator->publicKey, *ack);
            }
            backend->pendingSends.erase(iterator);
            break;
        }
    }
}

bool onAck(Backend* backend, const MeshCorePacket& packet) {
    uint32_t ackHash;
    if (!meshcore_parse_ack(packet, &ackHash)) {
        return false;
    }
    completeSend(backend, ackHash, &packet, nullptr);
    return true;
}

/** A recipient answers a flooded message with the path it took, and the acknowledgement as extra */
bool onPathReturn(Backend* backend, const MeshCorePacket& packet) {
    if (packet.payloadLength < 2 || packet.payload[0] != backend->identity.publicKey[0]) {
        return false;
    }
    for (const auto& contact : backend->contacts) {
        MeshCorePathReturn result;
        if (contact.publicKey[0] != packet.payload[1] || !meshcore_parse_path_return(backend->identity, contact.publicKey, packet, &result)) {
            continue;
        }
        if (result.hasAck) {
            KnownPath path {};
            path.length = result.hopCount;
            path.hashSize = result.hashSize;
            memcpy(path.hops, result.hops, static_cast<size_t>(result.hopCount) * result.hashSize);
            completeSend(backend, result.ackHash, nullptr, &path);
        }
        return true;
    }
    return false;
}

// endregion

// region MeshBackendApi

void* create(MeshHost* host) {
    auto* backend = tt::newObject<Backend>();
    if (backend == nullptr) {
        return nullptr;
    }
    backend->host = host;
    loadOrCreateIdentity(backend);
    loadContacts(backend);
    loadChannels(backend);
    return backend;
}

void destroy(void* context) {
    tt::deleteObject(static_cast<Backend*>(context));
}

constexpr size_t PRESET_DEFAULT = 0;

/** The default preset follows the region of the radio */
struct DefaultProfile {
    RadioRegion region;
    int32_t frequency;
    int32_t bandwidth;
    int32_t spreadingFactor;
    int32_t codingRate;
};

constexpr std::array DEFAULT_PROFILES = {
    DefaultProfile { .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8 },
    DefaultProfile { .region = RADIO_REGION_US_915, .frequency = 910525000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5 },
    DefaultProfile { .region = RADIO_REGION_AU_915, .frequency = 915800000, .bandwidth = 250000, .spreadingFactor = 10, .codingRate = 5 },
};

/** The community suggested settings per country. Preset index 0 is the default, so these start at index 1. */
struct Profile {
    const char* name;
    /** The region that is selected for the preset when the current region doesn't allow its frequency */
    RadioRegion region;
    int32_t frequency;
    int32_t bandwidth;
    int32_t spreadingFactor;
    int32_t codingRate;
    /** Bytes per hop in the path */
    int32_t pathHashSize;
    /** Empty for no scope */
    const char* regionScope;
};

constexpr std::array PROFILES = {
    Profile { .name = "Australia", .region = RADIO_REGION_AU_915, .frequency = 915800000, .bandwidth = 250000, .spreadingFactor = 10, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Australia (Narrow)", .region = RADIO_REGION_AU_915, .frequency = 916575000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 7, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Australia (Mid)", .region = RADIO_REGION_AU_915, .frequency = 915075000, .bandwidth = 125000, .spreadingFactor = 9, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Australia: SA, WA", .region = RADIO_REGION_AU_915, .frequency = 923125000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Australia: QLD", .region = RADIO_REGION_AU_915, .frequency = 923125000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Brazil", .region = RADIO_REGION_AU_915, .frequency = 923125000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Canada", .region = RADIO_REGION_US_915, .frequency = 910525000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 3, .regionScope = "" },
    Profile { .name = "Costa Rica", .region = RADIO_REGION_US_915, .frequency = 910525000, .bandwidth = 125000, .spreadingFactor = 11, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "EU/UK (Narrow)", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "EU/UK (Deprecated)", .region = RADIO_REGION_EU_868, .frequency = 869525000, .bandwidth = 250000, .spreadingFactor = 11, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Czech Republic (Narrow)", .region = RADIO_REGION_EU_868, .frequency = 869432000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 2, .regionScope = "" },
    Profile { .name = "EU 433MHz (Long Range)", .region = RADIO_REGION_EU_433, .frequency = 433650000, .bandwidth = 250000, .spreadingFactor = 11, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "EU 433MHz (Narrow)", .region = RADIO_REGION_EU_433, .frequency = 433650000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Hungary", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 2, .regionScope = "" },
    Profile { .name = "Netherlands", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 2, .regionScope = "nl" },
    Profile { .name = "Netherlands (Limburg)", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8, .pathHashSize = 2, .regionScope = "nl" },
    Profile { .name = "New Zealand (Narrow)", .region = RADIO_REGION_AU_915, .frequency = 917375000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 2, .regionScope = "" },
    Profile { .name = "New Zealand (Gisborne)", .region = RADIO_REGION_AU_915, .frequency = 917375000, .bandwidth = 250000, .spreadingFactor = 11, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Portugal 433", .region = RADIO_REGION_EU_433, .frequency = 433375000, .bandwidth = 62500, .spreadingFactor = 9, .codingRate = 6, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Portugal 868", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 6, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Slovakia", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 2, .regionScope = "" },
    Profile { .name = "Switzerland", .region = RADIO_REGION_EU_868, .frequency = 869618000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 8, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "USA", .region = RADIO_REGION_US_915, .frequency = 910525000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "USA - Southern California", .region = RADIO_REGION_US_915, .frequency = 927875000, .bandwidth = 62500, .spreadingFactor = 7, .codingRate = 5, .pathHashSize = 3, .regionScope = "" },
    Profile { .name = "Vietnam (Narrow)", .region = RADIO_REGION_US_915, .frequency = 920250000, .bandwidth = 62500, .spreadingFactor = 8, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
    Profile { .name = "Vietnam (Deprecated)", .region = RADIO_REGION_US_915, .frequency = 920250000, .bandwidth = 250000, .spreadingFactor = 11, .codingRate = 5, .pathHashSize = 1, .regionScope = "" },
};

/** "Default", the names of PROFILES, and a terminating nullptr */
constexpr auto presets = [] {
    std::array<const char*, PROFILES.size() + 2> names {};
    names[0] = "Default";
    for (size_t i = 0; i < PROFILES.size(); i++) {
        names[i + 1] = PROFILES[i].name;
    }
    return names;
}();

/** Whether the whole channel of width @a bandwidth around @a frequency lies in one band of @a region */
bool fitsRegion(int32_t frequency, int32_t bandwidth, RadioRegion region) {
    const RadioRegionBand* bands = nullptr;
    const size_t count = radio_get_region_bands(region, &bands);
    const int64_t low = static_cast<int64_t>(frequency) - bandwidth / 2;
    const int64_t high = static_cast<int64_t>(frequency) + bandwidth / 2;
    return std::any_of(bands, bands + count, [=](const RadioRegionBand& band) {
        return low >= band.frequency_min && high <= band.frequency_max;
    });
}

void getPresetNetwork(size_t preset, MeshNetworkSettings* network) {
    *network = { .path_hash_size = 1, .region_scope = {} };
    if (preset != PRESET_DEFAULT && preset <= PROFILES.size()) {
        const Profile& profile = PROFILES[preset - 1];
        network->path_hash_size = profile.pathHashSize;
        strncpy(network->region_scope, profile.regionScope, MESH_NAME_MAX_LENGTH);
    }
}

error_t getPresetRegion(size_t preset, RadioRegion current, RadioRegion* region) {
    *region = current;
    if (preset == PRESET_DEFAULT || preset > PROFILES.size()) {
        return ERROR_NONE;
    }
    const Profile& profile = PROFILES[preset - 1];
    if (fitsRegion(profile.frequency, profile.bandwidth, current)) {
        return ERROR_NONE;
    }
    if (fitsRegion(profile.frequency, profile.bandwidth, profile.region)) {
        *region = profile.region;
        return ERROR_NONE;
    }
    return ERROR_NOT_SUPPORTED;
}

error_t getRadioProfile(void*, RadioRegion region, RadioModulation modulation, size_t preset, MeshRadioProfile* profile) {
    // MeshCore's ESP-NOW transport broadcasts unencrypted frames on the Wi-Fi channel that the radio is on, at 20 dBm.
    // The region doesn't matter, as the Wi-Fi stack limits the power for the country.
    if (modulation == RADIO_MODULATION_ESPNOW) {
        *profile = {
            .modulation = RADIO_MODULATION_ESPNOW,
            .frequency = 0,
            .bandwidth = 0,
            .spreading_factor = 0,
            .coding_rate = 0,
            .sync_word = 0,
            .preamble_length = 0,
            .power = 20,
            .channel = 1,
        };
        return ERROR_NONE;
    }

    if (modulation != RADIO_MODULATION_LORA) {
        return ERROR_NOT_SUPPORTED;
    }

    int32_t frequency = 0;
    int32_t bandwidth = 0;
    int32_t spreadingFactor = 0;
    int32_t codingRate = 0;
    if (preset == PRESET_DEFAULT) {
        const auto* match = std::find_if(DEFAULT_PROFILES.begin(), DEFAULT_PROFILES.end(), [region](const DefaultProfile& candidate) { return candidate.region == region; });
        if (match == DEFAULT_PROFILES.end()) {
            return ERROR_NOT_SUPPORTED;
        }
        frequency = match->frequency;
        bandwidth = match->bandwidth;
        spreadingFactor = match->spreadingFactor;
        codingRate = match->codingRate;
    } else if (preset <= PROFILES.size()) {
        const Profile& match = PROFILES[preset - 1];
        if (!fitsRegion(match.frequency, match.bandwidth, region)) {
            return ERROR_NOT_SUPPORTED;
        }
        frequency = match.frequency;
        bandwidth = match.bandwidth;
        spreadingFactor = match.spreadingFactor;
        codingRate = match.codingRate;
    } else {
        return ERROR_NOT_SUPPORTED;
    }
    // Sync word 0x12 is LoRa's private network sync word
    *profile = {
        .modulation = RADIO_MODULATION_LORA,
        .frequency = frequency,
        .bandwidth = bandwidth,
        .spreading_factor = spreadingFactor,
        .coding_rate = codingRate,
        .sync_word = 0x12,
        .preamble_length = 16,
        .power = 22,
        .channel = 0,
    };
    return ERROR_NONE;
}

/** A zero hop advert only reaches direct neighbours, which keeps frequent restarts from flooding the network */
void onStart(void* context) {
    auto* backend = static_cast<Backend*>(context);
    loadLastFloodAdvertTime(backend);
    backend->floodAdvertPending = true;
    sendAdvert(backend, MeshCoreRoute::Direct);
}

error_t getContactUri(void* context, const MeshAddress* contact, const char* name, char* uri, size_t uriSize) {
    auto* backend = static_cast<Backend*>(context);
    if (contact != nullptr && contact->length != MESHCORE_PUBLIC_KEY_SIZE) {
        return ERROR_INVALID_ARGUMENT;
    }
    const uint8_t* publicKey = (contact != nullptr) ? contact->bytes : backend->identity.publicKey;
    // Contacts are shown as companions, as the type of a contact isn't stored
    return meshcore_build_contact_uri(name, publicKey, 1, uri, uriSize) ? ERROR_NONE : ERROR_BUFFER_OVERFLOW;
}

bool isValidContactAddress(void*, const MeshAddress* address) {
    return address->length == MESHCORE_PUBLIC_KEY_SIZE;
}

error_t sendAdvertOnRequest(void* context, MeshAdvertRoute route) {
    return sendAdvert(static_cast<Backend*>(context), route == MESH_ADVERT_ZERO_HOP ? MeshCoreRoute::Direct : MeshCoreRoute::Flood) ? ERROR_NONE : ERROR_RESOURCE;
}

void onNodeNameChanged(void* context) {
    sendAdvert(static_cast<Backend*>(context));
}

bool onReceive(void* context, const uint8_t* data, size_t length, float rssi, float snr) {
    auto* backend = static_cast<Backend*>(context);
    MeshCorePacket packet;
    if (!meshcore_packet_parse(data, length, &packet)) {
        return false;
    }

    const uint32_t hash = meshcore_packet_hash(packet);
    if (packet.payloadType == MeshCorePayload::GroupText) {
        for (auto& send : backend->channelSends) {
            if (send.packetHash == hash) {
                if (send.repeats < UINT8_MAX) {
                    send.repeats++;
                }
                mesh_host_set_repeats(backend->host, send.seq, send.repeats);
                return true;
            }
        }
    }
    if (std::find(backend->seenPackets.begin(), backend->seenPackets.end(), hash) != backend->seenPackets.end()) {
        return true;
    }
    backend->seenPackets.push_back(hash);
    while (backend->seenPackets.size() > SEEN_PACKETS_LIMIT) {
        backend->seenPackets.pop_front();
    }

    switch (packet.payloadType) {
        case MeshCorePayload::Advert:
            return onAdvert(backend, packet, rssi, snr);
        case MeshCorePayload::GroupText:
            return onGroupText(backend, packet, rssi, snr);
        case MeshCorePayload::TextMessage:
            return onTextMessage(backend, packet, rssi, snr);
        case MeshCorePayload::Ack:
            return onAck(backend, packet);
        case MeshCorePayload::Path:
            return onPathReturn(backend, packet);
        default:
            // Payload types this backend doesn't support, e.g. path and trace packets
            return false;
    }
}

/** Builds an attempt: over the known path for the first ones, flooded after that */
bool buildAttempt(Backend* backend, const PendingSend& send, uint8_t attempt, MeshCorePacket* packet, uint32_t* ackHash) {
    if (!meshcore_build_text_message(backend->identity, send.publicKey, send.timestamp, attempt, send.text.c_str(), packet, ackHash)) {
        return false;
    }
    if (attempt < send.pathAttempts) {
        packet->route = MeshCoreRoute::Direct;
        packet->pathLength = send.path.length;
        packet->pathHashSize = send.path.hashSize;
        memcpy(packet->path, send.path.hops, static_cast<size_t>(send.path.length) * send.path.hashSize);
    }
    return true;
}

/** Sends the message again as the next attempt */
bool retry(Backend* backend, PendingSend& send) {
    MeshCorePacket packet;
    const uint8_t attempt = send.attempt + 1;
    uint32_t ackHash;
    if (!buildAttempt(backend, send, attempt, &packet, &ackHash) || !transmit(backend, packet, send.seq)) {
        return false;
    }
    send.attempt = attempt;
    mesh_host_set_retry(backend->host, send.seq, attempt, MAX_RETRIES);
    send.ackHashes[attempt] = ackHash;
    send.deadlineMs = WAITING_FOR_TRANSMISSION;
    return true;
}

void onTick(void* context) {
    auto* backend = static_cast<Backend*>(context);
    const uint64_t nowMs = getMillis();
    if (backend->floodAdvertPending && nowMs >= backend->nextFloodAdvertMs && isWallTimeValid()) {
        const auto now = static_cast<uint32_t>(time(nullptr));
        // A saved time in the future was saved with a wrong clock
        const bool recent = backend->lastFloodAdvertTime != 0 && backend->lastFloodAdvertTime <= now &&
            now - backend->lastFloodAdvertTime < ADVERT_INTERVAL_MS / 1000;
        // A failed advert stays pending
        backend->floodAdvertPending = !recent && !sendAdvert(backend);
    }
    // Without a valid clock the advert would carry an uptime as its time, which other nodes ignore
    if (nowMs >= backend->nextFloodAdvertMs && nowMs - backend->lastAdvertMs >= ADVERT_INTERVAL_MS && isWallTimeValid()) {
        sendAdvert(backend);
    }
    for (auto iterator = backend->pendingSends.begin(); iterator != backend->pendingSends.end();) {
        if (nowMs < iterator->deadlineMs) {
            ++iterator;
            continue;
        }
        if (iterator->attempt < MAX_RETRIES && retry(backend, *iterator)) {
            ++iterator;
        } else {
            mesh_host_set_delivery(backend->host, iterator->seq, MESH_DELIVERY_FAILED);
            if (iterator->pathAttempts > 0) {
                forgetPath(backend, iterator->publicKey);
            }
            iterator = backend->pendingSends.erase(iterator);
        }
    }
}

error_t sendText(void* context, uint32_t seq, const MeshAddress* to, uint8_t channelIndex, const char* text) {
    auto* backend = static_cast<Backend*>(context);
    MeshCorePacket packet;

    if (to == nullptr) {
        for (const auto& channel : backend->channels) {
            if (channel.index != channelIndex) {
                continue;
            }
            char name[MESH_NAME_MAX_LENGTH + 1];
            mesh_host_get_node_name(backend->host, name, sizeof(name));
            if (!meshcore_build_group_text(channel.channel, getTimestamp(), name, text, &packet)) {
                return ERROR_OUT_OF_RANGE;
            }
            if (!transmit(backend, packet, seq)) {
                return ERROR_RESOURCE;
            }
            backend->channelSends.push_back({ .seq = seq, .packetHash = meshcore_packet_hash(packet), .repeats = 0 });
            while (backend->channelSends.size() > CHANNEL_SENDS_LIMIT) {
                backend->channelSends.pop_front();
            }
            return ERROR_NONE;
        }
        return ERROR_NOT_FOUND;
    }

    if (to->length != MESHCORE_PUBLIC_KEY_SIZE) {
        return ERROR_INVALID_ARGUMENT;
    }
    PendingSend send {};
    send.seq = seq;
    memcpy(send.publicKey, to->bytes, MESHCORE_PUBLIC_KEY_SIZE);
    send.timestamp = getTimestamp();
    send.text = text;
    if (const KnownPath* path = findKnownPath(backend, to->bytes)) {
        send.path = *path;
        send.pathAttempts = PATH_ATTEMPTS;
    }
    uint32_t ackHash;
    if (!buildAttempt(backend, send, 0, &packet, &ackHash)) {
        return ERROR_OUT_OF_RANGE;
    }
    if (!transmit(backend, packet, seq)) {
        return ERROR_RESOURCE;
    }
    send.ackHashes[0] = ackHash;
    send.deadlineMs = WAITING_FOR_TRANSMISSION;
    backend->pendingSends.push_back(send);
    return ERROR_NONE;
}

void onFrameDone(void* context, uint32_t seq, bool transmitted) {
    auto* backend = static_cast<Backend*>(context);
    for (auto iterator = backend->pendingSends.begin(); iterator != backend->pendingSends.end(); ++iterator) {
        if (iterator->seq != seq) {
            continue;
        }
        if (transmitted) {
            iterator->deadlineMs = getMillis() + ACK_TIMEOUT_MS;
        } else {
            backend->pendingSends.erase(iterator);
        }
        return;
    }
}

void cancelSend(void* context, uint32_t seq) {
    auto* backend = static_cast<Backend*>(context);
    std::erase_if(backend->pendingSends, [seq](const PendingSend& send) { return send.seq == seq; });
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
        memcpy(out.key, channel.key, 16);
        out.key_length = 16;
    }
    return ERROR_NONE;
}

error_t setChannel(void* context, const MeshChannel* channel) {
    auto* backend = static_cast<Backend*>(context);
    uint8_t key[16];
    if (channel->key_length == 16) {
        memcpy(key, channel->key, sizeof(key));
    } else if (channel->key_length == 0 && channel->name[0] == '#') {
        meshcore_hashtag_channel_key(channel->name, key);
    } else if (channel->key_length == 0 && strcmp(channel->name, PUBLIC_CHANNEL_NAME) == 0) {
        memcpy(key, MESHCORE_PUBLIC_CHANNEL_KEY, sizeof(key));
    } else {
        return ERROR_INVALID_ARGUMENT;
    }
    const bool exists = std::any_of(backend->channels.begin(), backend->channels.end(), [channel](const Channel& existing) {
        return existing.index == channel->index;
    });
    if (!exists && backend->channels.size() >= MAX_CHANNELS) {
        return ERROR_RESOURCE;
    }
    addChannel(backend, channel->index, channel->name, key);
    saveChannels(backend);
    return ERROR_NONE;
}

error_t removeChannel(void* context, uint8_t index) {
    auto* backend = static_cast<Backend*>(context);
    const size_t removed = std::erase_if(backend->channels, [index](const Channel& existing) { return existing.index == index; });
    if (removed == 0) {
        return ERROR_NOT_FOUND;
    }
    saveChannels(backend);
    return ERROR_NONE;
}

// endregion

} // namespace

const MeshBackendApi meshcore_backend_api = {
    .id = "meshcore",
    .create = create,
    .destroy = destroy,
    .presets = presets.data(),
    .get_radio_profile = getRadioProfile,
    .get_preset_region = getPresetRegion,
    .get_preset_network = getPresetNetwork,
    .on_start = onStart,
    .on_receive = onReceive,
    .on_tick = onTick,
    .send_text = sendText,
    .get_channels = getChannels,
    .set_channel = setChannel,
    .get_contact_uri = getContactUri,
    .is_valid_contact_address = isValidContactAddress,
    .on_node_name_changed = onNodeNameChanged,
    .remove_channel = removeChannel,
    .send_advert = sendAdvertOnRequest,
    .on_frame_done = onFrameDone,
    .cancel_send = cancelSend,
    .features = MESH_FEATURE_NAMED_CHANNELS | MESH_FEATURE_KEYED_CHANNELS | MESH_FEATURE_ADVERTS,
};
