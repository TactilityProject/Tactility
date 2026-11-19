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
    /** The 32-byte Curve25519 public key, empty when the node has none */
    tt::std::vector<uint8_t> publicKey;
};

constexpr size_t MESHTASTIC_PUBLIC_KEY_SIZE = 32;
/** The authentication tag and the extra nonce that follow a public key encrypted payload */
constexpr size_t MESHTASTIC_PKI_OVERHEAD = 12;

struct MeshtasticKeyPair {
    uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE];
    uint8_t publicKey[MESHTASTIC_PUBLIC_KEY_SIZE];
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

/** Generates a Curve25519 key pair */
bool meshtastic_generate_key_pair(MeshtasticKeyPair* keyPair);

/** Derives the public key of a Curve25519 private key */
bool meshtastic_public_key_from_private(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint8_t publicKey[MESHTASTIC_PUBLIC_KEY_SIZE]);

/**
 * Public key encryption of a direct message: AES-256-CCM with an 8-byte tag, keyed by the SHA-256 of the X25519 shared secret.
 * @param[out] output receives @a length + MESHTASTIC_PKI_OVERHEAD bytes: the ciphertext, the tag and the 4-byte extra nonce
 * @param[in] extraNonce must be random per packet, and not 0
 */
bool meshtastic_pki_encrypt(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], const uint8_t peerPublicKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint32_t packetId, uint32_t from, uint32_t extraNonce, const uint8_t* input, size_t length, uint8_t* output);

/**
 * @param[in] length the size of @a input including MESHTASTIC_PKI_OVERHEAD
 * @param[out] output receives @a length - MESHTASTIC_PKI_OVERHEAD bytes
 * @return false when the tag doesn't match
 */
bool meshtastic_pki_decrypt(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], const uint8_t peerPublicKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint32_t packetId, uint32_t from, const uint8_t* input, size_t length, uint8_t* output);

tt::std::vector<uint8_t> meshtastic_encode_data(const MeshtasticData& data);

/** @return false when the bytes are not a valid Data message */
bool meshtastic_decode_data(const uint8_t* bytes, size_t length, MeshtasticData* data);

tt::std::vector<uint8_t> meshtastic_encode_user(const MeshtasticUser& user);

bool meshtastic_decode_user(const uint8_t* bytes, size_t length, MeshtasticUser* user);

/** A Routing message with error_reason NONE, the payload of an acknowledgement */
tt::std::vector<uint8_t> meshtastic_encode_routing_ack();

/** @return the node id in Meshtastic's "!aabbccdd" notation */
tt::std::string meshtastic_node_id(uint32_t nodeNum);
