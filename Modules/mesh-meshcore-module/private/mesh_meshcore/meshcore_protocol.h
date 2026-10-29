// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <mesh_meshcore/meshcore_crypto.h>
#include <mesh_meshcore/meshcore_packet.h>

#include <tactility/std/string.hpp>

#include <cstddef>
#include <cstdint>

struct MeshCoreIdentity {
    uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE];
    uint8_t privateKey[MESHCORE_PRIVATE_KEY_SIZE];
};

/** A group channel. The hash is the first byte of SHA-256 over the 16-byte key. */
struct MeshCoreChannel {
    uint8_t hash;
    uint8_t secret[MESHCORE_SECRET_SIZE];
};

enum class MeshCoreNodeType : uint8_t {
    None = 0,
    Chat = 1,
    Repeater = 2,
    Room = 3,
    Sensor = 4,
};

struct MeshCoreAdvert {
    uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE];
    uint32_t timestamp;
    MeshCoreNodeType type;
    tt::std::string name;
};

struct MeshCoreText {
    uint32_t timestamp;
    /** Text type in the upper 6 bits, attempt in the lower 2 bits */
    uint8_t flags;
    tt::std::string text;
};

/** The well-known key of the "Public" channel */
extern const uint8_t MESHCORE_PUBLIC_CHANNEL_KEY[16];

void meshcore_identity_create(MeshCoreIdentity* identity);

void meshcore_channel_from_key(const uint8_t key[16], MeshCoreChannel* channel);

/** Key of a hashtag channel (e.g. "#test"): the first 16 bytes of the SHA-256 of its name */
void meshcore_hashtag_channel_key(const char* name, uint8_t key[16]);

/** Key of a region scope: the key of the hashtag channel with the same name, which gets a "#" in front when it has none */
void meshcore_scope_key(const char* name, uint8_t key[16]);

/**
 * The code that identifies a packet as part of a region scope: the first 2 bytes of an HMAC-SHA256 over the payload type
 * and the payload, keyed with the scope's key. 0x0000 and 0xFFFF are reserved, so they are moved to the nearest other code.
 */
uint16_t meshcore_transport_code(const uint8_t key[16], const MeshCorePacket& packet);

/**
 * Build the URI that MeshCore apps scan as a QR code to add a contact:
 * meshcore://contact/add?name=<name>&public_key=<hex>&type=<type>
 * @param[in] type the contact type: 1 for a companion, 2 for a repeater, 3 for a room server, 4 for a sensor
 * @return false when it doesn't fit in @a uri
 */
bool meshcore_build_contact_uri(const char* name, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE], uint8_t type, char* uri, size_t uriSize);

/** Shared secret between our identity and a peer's public key */
void meshcore_shared_secret(const MeshCoreIdentity& identity, const uint8_t peerPublicKey[MESHCORE_PUBLIC_KEY_SIZE], uint8_t secret[MESHCORE_SECRET_SIZE]);

bool meshcore_build_advert(const MeshCoreIdentity& identity, uint32_t timestamp, const char* name, MeshCorePacket* packet);

/** @return false when the payload is malformed or the signature is invalid */
bool meshcore_parse_advert(const MeshCorePacket& packet, MeshCoreAdvert* advert);

/** @param senderName prefixed to the text as "name: text", as MeshCore clients do */
bool meshcore_build_group_text(const MeshCoreChannel& channel, uint32_t timestamp, const char* senderName, const char* text, MeshCorePacket* packet);

/** @return false when the channel hash or MAC doesn't match */
bool meshcore_parse_group_text(const MeshCoreChannel& channel, const MeshCorePacket& packet, MeshCoreText* text);

/**
 * @param ackHash receives the hash the recipient acknowledges with
 */
bool meshcore_build_text_message(
    const MeshCoreIdentity& identity,
    const uint8_t peerPublicKey[MESHCORE_PUBLIC_KEY_SIZE],
    uint32_t timestamp,
    uint8_t attempt,
    const char* text,
    MeshCorePacket* packet,
    uint32_t* ackHash
);

/** Destination and source hash of a text message, without decrypting */
bool meshcore_peek_text_message(const MeshCorePacket& packet, uint8_t* destinationHash, uint8_t* sourceHash);

/**
 * Decrypt a text message from a known peer.
 * @param ackHash receives the hash to acknowledge with
 * @return false when the MAC doesn't match, e.g. because it's from another peer with the same hash
 */
bool meshcore_parse_text_message(
    const MeshCoreIdentity& identity,
    const uint8_t peerPublicKey[MESHCORE_PUBLIC_KEY_SIZE],
    const MeshCorePacket& packet,
    MeshCoreText* text,
    uint32_t* ackHash
);

void meshcore_build_ack(uint32_t ackHash, MeshCorePacket* packet);

bool meshcore_parse_ack(const MeshCorePacket& packet, uint32_t* ackHash);
