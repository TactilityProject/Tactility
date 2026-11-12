// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <cstddef>
#include <cstdint>

constexpr uint32_t MESHTASTIC_BROADCAST = 0xFFFFFFFF;
constexpr size_t MESHTASTIC_HEADER_SIZE = 16;
constexpr size_t MESHTASTIC_MAX_PAYLOAD_SIZE = 237;
constexpr uint8_t MESHTASTIC_DEFAULT_HOP_LIMIT = 3;

/** Port numbers of the Data payload */
enum class MeshtasticPort : uint32_t {
    TextMessage = 1,
    Position = 3,
    NodeInfo = 4,
    Routing = 5,
};

/** The 16-byte over-the-air header, little-endian */
struct MeshtasticHeader {
    uint32_t to = MESHTASTIC_BROADCAST;
    uint32_t from = 0;
    uint32_t id = 0;
    uint8_t hopLimit = MESHTASTIC_DEFAULT_HOP_LIMIT;
    bool wantAck = false;
    bool viaMqtt = false;
    uint8_t hopStart = MESHTASTIC_DEFAULT_HOP_LIMIT;
    uint8_t channelHash = 0;
    uint8_t nextHop = 0;
    uint8_t relayNode = 0;
};

/** The decoded Data protobuf, limited to the fields this backend uses */
struct MeshtasticData {
    MeshtasticPort port = MeshtasticPort::TextMessage;
    tt::std::vector<uint8_t> payload;
    bool wantResponse = false;
    uint32_t requestId = 0;
};

/** The decoded User protobuf (NodeInfo payload) */
struct MeshtasticUser {
    tt::std::string id;
    tt::std::string longName;
    tt::std::string shortName;
};

struct MeshtasticChannelKey {
    uint8_t bytes[32];
    /** 16 for AES-128, 32 for AES-256 */
    uint8_t length;
};

/** The default key, used by a PSK of 0x01 */
extern const uint8_t MESHTASTIC_DEFAULT_KEY[16];

/**
 * Expand a PSK as configured in a channel: 1 byte selects the default key (1) or a variant of it (2-10),
 * 16 or 32 bytes are used as-is.
 * @return false for an invalid length or 0x00 (no encryption, which is not supported)
 */
bool meshtastic_expand_psk(const uint8_t* psk, size_t length, MeshtasticChannelKey* key);

/** The channel hash: XOR of the name bytes, XORed with the XOR of the key bytes */
uint8_t meshtastic_channel_hash(const char* name, const MeshtasticChannelKey& key);

/**
 * The frequency slot of a channel within a band: djb2(name) modulo the number of channels that fit.
 * @return the center frequency in Hz
 */
int32_t meshtastic_channel_frequency(const char* name, int32_t bandStart, int32_t bandEnd, int32_t bandwidth);

void meshtastic_write_header(const MeshtasticHeader& header, uint8_t data[MESHTASTIC_HEADER_SIZE]);

bool meshtastic_read_header(const uint8_t* data, size_t length, MeshtasticHeader* header);

/** AES-CTR with the nonce made from the packet id and sender. Encrypts and decrypts. */
bool meshtastic_crypt(const MeshtasticChannelKey& key, uint32_t packetId, uint32_t from, const uint8_t* input, uint8_t* output, size_t length);

tt::std::vector<uint8_t> meshtastic_encode_data(const MeshtasticData& data);

/** @return false when the bytes are not a valid Data message */
bool meshtastic_decode_data(const uint8_t* bytes, size_t length, MeshtasticData* data);

tt::std::vector<uint8_t> meshtastic_encode_user(const MeshtasticUser& user);

bool meshtastic_decode_user(const uint8_t* bytes, size_t length, MeshtasticUser* user);

/** A Routing message with error_reason NONE, the payload of an acknowledgement */
tt::std::vector<uint8_t> meshtastic_encode_routing_ack();

/** @return the node id in Meshtastic's "!aabbccdd" notation */
tt::std::string meshtastic_node_id(uint32_t nodeNum);
