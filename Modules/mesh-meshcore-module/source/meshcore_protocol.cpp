// SPDX-License-Identifier: Apache-2.0
#include <mesh_meshcore/meshcore_protocol.h>

#include <ed25519.h>

#include <mesh/mesh.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr uint8_t ADVERT_FLAG_LATLON = 0x10;
constexpr uint8_t ADVERT_FLAG_FEATURE_1 = 0x20;
constexpr uint8_t ADVERT_FLAG_FEATURE_2 = 0x40;
constexpr uint8_t ADVERT_FLAG_NAME = 0x80;
constexpr size_t ADVERT_FIXED_SIZE = MESHCORE_PUBLIC_KEY_SIZE + 4 + MESHCORE_SIGNATURE_SIZE;
constexpr size_t TEXT_HEADER_SIZE = 5;
constexpr size_t MAX_PLAINTEXT_SIZE = 160;

void writeU32(uint8_t* data, uint32_t value) {
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

uint32_t readU32(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
}

uint32_t ackHashOf(const uint8_t* plaintext, size_t length, const uint8_t senderPublicKey[MESHCORE_PUBLIC_KEY_SIZE]) {
    uint8_t hash[4];
    meshcore_sha256_pair(hash, sizeof(hash), plaintext, length, senderPublicKey, MESHCORE_PUBLIC_KEY_SIZE);
    return readU32(hash);
}

/** Decrypted plaintext is zero-padded to whole blocks */
tt::std::string textFromPlaintext(const uint8_t* plaintext, size_t length) {
    return tt::std::string(reinterpret_cast<const char*>(plaintext), strnlen(reinterpret_cast<const char*>(plaintext), length));
}

} // namespace

const uint8_t MESHCORE_PUBLIC_CHANNEL_KEY[16] = {
    0x8b, 0x33, 0x87, 0xe9, 0xc5, 0xcd, 0xea, 0x6a, 0xc9, 0xe5, 0xed, 0xba, 0xa1, 0x15, 0xcd, 0x72
};

void meshcore_identity_create(MeshCoreIdentity* identity) {
    uint8_t seed[32];
    meshcore_random(seed, sizeof(seed));
    ed25519_create_keypair(identity->publicKey, identity->privateKey, seed);
}

void meshcore_channel_from_key(const uint8_t key[16], MeshCoreChannel* channel) {
    memset(channel->secret, 0, sizeof(channel->secret));
    memcpy(channel->secret, key, 16);
    meshcore_sha256(&channel->hash, 1, key, 16);
}

void meshcore_hashtag_channel_key(const char* name, uint8_t key[16]) {
    meshcore_sha256(key, 16, reinterpret_cast<const uint8_t*>(name), strlen(name));
}

void meshcore_scope_key(const char* name, uint8_t key[16]) {
    char hashtag[MESH_NAME_MAX_LENGTH + 2];
    snprintf(hashtag, sizeof(hashtag), "%s%s", name[0] == '#' ? "" : "#", name);
    meshcore_hashtag_channel_key(hashtag, key);
}

uint16_t meshcore_transport_code(const uint8_t key[16], const MeshCorePacket& packet) {
    uint8_t message[1 + MESHCORE_MAX_PAYLOAD_SIZE];
    message[0] = static_cast<uint8_t>(packet.payloadType);
    memcpy(message + 1, packet.payload, packet.payloadLength);
    uint8_t mac[32];
    meshcore_hmac_sha256(key, 16, message, 1 + packet.payloadLength, mac);
    const uint16_t code = static_cast<uint16_t>(mac[0] | (mac[1] << 8));
    if (code == 0x0000) {
        return 0x0001;
    }
    return (code == 0xFFFF) ? 0xFFFE : code;
}

void meshcore_shared_secret(const MeshCoreIdentity& identity, const uint8_t peerPublicKey[MESHCORE_PUBLIC_KEY_SIZE], uint8_t secret[MESHCORE_SECRET_SIZE]) {
    ed25519_key_exchange(secret, peerPublicKey, identity.privateKey);
}

bool meshcore_build_advert(const MeshCoreIdentity& identity, uint32_t timestamp, const char* name, MeshCorePacket* packet) {
    const size_t nameLength = strlen(name);
    if (ADVERT_FIXED_SIZE + 1 + nameLength > MESHCORE_MAX_PAYLOAD_SIZE) {
        return false;
    }

    *packet = {};
    packet->route = MeshCoreRoute::Flood;
    packet->payloadType = MeshCorePayload::Advert;

    uint8_t* payload = packet->payload;
    memcpy(payload, identity.publicKey, MESHCORE_PUBLIC_KEY_SIZE);
    writeU32(payload + MESHCORE_PUBLIC_KEY_SIZE, timestamp);
    uint8_t* appData = payload + ADVERT_FIXED_SIZE;
    appData[0] = (uint8_t)MeshCoreNodeType::Chat | ADVERT_FLAG_NAME;
    memcpy(appData + 1, name, nameLength);
    const size_t appDataLength = 1 + nameLength;

    // The signature covers the public key, timestamp and app data
    uint8_t message[MESHCORE_PUBLIC_KEY_SIZE + 4 + MESHCORE_MAX_PAYLOAD_SIZE];
    memcpy(message, payload, MESHCORE_PUBLIC_KEY_SIZE + 4);
    memcpy(message + MESHCORE_PUBLIC_KEY_SIZE + 4, appData, appDataLength);
    ed25519_sign(payload + MESHCORE_PUBLIC_KEY_SIZE + 4, message, MESHCORE_PUBLIC_KEY_SIZE + 4 + appDataLength, identity.publicKey, identity.privateKey);

    packet->payloadLength = (uint8_t)(ADVERT_FIXED_SIZE + appDataLength);
    return true;
}

bool meshcore_parse_advert(const MeshCorePacket& packet, MeshCoreAdvert* advert) {
    if (packet.payloadType != MeshCorePayload::Advert || packet.payloadLength < ADVERT_FIXED_SIZE) {
        return false;
    }
    const uint8_t* payload = packet.payload;
    const uint8_t* appData = payload + ADVERT_FIXED_SIZE;
    const size_t appDataLength = packet.payloadLength - ADVERT_FIXED_SIZE;

    uint8_t message[MESHCORE_PUBLIC_KEY_SIZE + 4 + MESHCORE_MAX_PAYLOAD_SIZE];
    memcpy(message, payload, MESHCORE_PUBLIC_KEY_SIZE + 4);
    memcpy(message + MESHCORE_PUBLIC_KEY_SIZE + 4, appData, appDataLength);
    if (ed25519_verify(payload + MESHCORE_PUBLIC_KEY_SIZE + 4, message, MESHCORE_PUBLIC_KEY_SIZE + 4 + appDataLength, payload) != 1) {
        return false;
    }

    memcpy(advert->publicKey, payload, MESHCORE_PUBLIC_KEY_SIZE);
    advert->timestamp = readU32(payload + MESHCORE_PUBLIC_KEY_SIZE);
    advert->type = MeshCoreNodeType::None;
    advert->name.clear();
    if (appDataLength == 0) {
        return true;
    }

    const uint8_t flags = appData[0];
    advert->type = static_cast<MeshCoreNodeType>(flags & 0x0F);
    size_t position = 1;
    if (flags & ADVERT_FLAG_LATLON) {
        position += 8;
    }
    if (flags & ADVERT_FLAG_FEATURE_1) {
        position += 2;
    }
    if (flags & ADVERT_FLAG_FEATURE_2) {
        position += 2;
    }
    if ((flags & ADVERT_FLAG_NAME) && position < appDataLength) {
        advert->name = textFromPlaintext(appData + position, appDataLength - position);
    }
    return true;
}

bool meshcore_build_group_text(const MeshCoreChannel& channel, uint32_t timestamp, const char* senderName, const char* text, MeshCorePacket* packet) {
    uint8_t plaintext[MAX_PLAINTEXT_SIZE];
    writeU32(plaintext, timestamp);
    plaintext[4] = 0;
    const int written = snprintf(reinterpret_cast<char*>(plaintext + TEXT_HEADER_SIZE), sizeof(plaintext) - TEXT_HEADER_SIZE, "%s: %s", senderName, text);
    if (written < 0 || (size_t)written >= sizeof(plaintext) - TEXT_HEADER_SIZE) {
        return false;
    }

    *packet = {};
    packet->route = MeshCoreRoute::Flood;
    packet->payloadType = MeshCorePayload::GroupText;
    packet->payload[0] = channel.hash;
    const size_t encrypted = meshcore_encrypt_then_mac(channel.secret, packet->payload + 1, plaintext, TEXT_HEADER_SIZE + written);
    if (encrypted == 0) {
        return false;
    }
    packet->payloadLength = (uint8_t)(1 + encrypted);
    return true;
}

bool meshcore_parse_group_text(const MeshCoreChannel& channel, const MeshCorePacket& packet, MeshCoreText* text) {
    if (packet.payloadType != MeshCorePayload::GroupText || packet.payloadLength < 1 + MESHCORE_CIPHER_MAC_SIZE + MESHCORE_CIPHER_BLOCK_SIZE ||
        packet.payload[0] != channel.hash) {
        return false;
    }
    uint8_t plaintext[MESHCORE_MAX_PAYLOAD_SIZE];
    const size_t length = meshcore_mac_then_decrypt(channel.secret, plaintext, packet.payload + 1, packet.payloadLength - 1);
    if (length < TEXT_HEADER_SIZE) {
        return false;
    }
    text->timestamp = readU32(plaintext);
    text->flags = plaintext[4];
    text->text = textFromPlaintext(plaintext + TEXT_HEADER_SIZE, length - TEXT_HEADER_SIZE);
    return true;
}

bool meshcore_build_text_message(
    const MeshCoreIdentity& identity,
    const uint8_t peerPublicKey[MESHCORE_PUBLIC_KEY_SIZE],
    uint32_t timestamp,
    uint8_t attempt,
    const char* text,
    MeshCorePacket* packet,
    uint32_t* ackHash
) {
    const size_t textLength = strlen(text);
    uint8_t plaintext[MAX_PLAINTEXT_SIZE];
    if (TEXT_HEADER_SIZE + textLength > sizeof(plaintext)) {
        return false;
    }
    writeU32(plaintext, timestamp);
    plaintext[4] = attempt & 0x03;
    memcpy(plaintext + TEXT_HEADER_SIZE, text, textLength);
    const size_t plaintextLength = TEXT_HEADER_SIZE + textLength;
    *ackHash = ackHashOf(plaintext, plaintextLength, identity.publicKey);

    uint8_t secret[MESHCORE_SECRET_SIZE];
    meshcore_shared_secret(identity, peerPublicKey, secret);

    *packet = {};
    packet->route = MeshCoreRoute::Flood;
    packet->payloadType = MeshCorePayload::TextMessage;
    packet->payload[0] = peerPublicKey[0];
    packet->payload[1] = identity.publicKey[0];
    const size_t encrypted = meshcore_encrypt_then_mac(secret, packet->payload + 2, plaintext, plaintextLength);
    if (encrypted == 0) {
        return false;
    }
    packet->payloadLength = (uint8_t)(2 + encrypted);
    return true;
}

bool meshcore_peek_text_message(const MeshCorePacket& packet, uint8_t* destinationHash, uint8_t* sourceHash) {
    if (packet.payloadType != MeshCorePayload::TextMessage || packet.payloadLength < 2 + MESHCORE_CIPHER_MAC_SIZE + MESHCORE_CIPHER_BLOCK_SIZE) {
        return false;
    }
    *destinationHash = packet.payload[0];
    *sourceHash = packet.payload[1];
    return true;
}

bool meshcore_parse_text_message(
    const MeshCoreIdentity& identity,
    const uint8_t peerPublicKey[MESHCORE_PUBLIC_KEY_SIZE],
    const MeshCorePacket& packet,
    MeshCoreText* text,
    uint32_t* ackHash
) {
    uint8_t secret[MESHCORE_SECRET_SIZE];
    meshcore_shared_secret(identity, peerPublicKey, secret);

    uint8_t plaintext[MESHCORE_MAX_PAYLOAD_SIZE];
    const size_t length = meshcore_mac_then_decrypt(secret, plaintext, packet.payload + 2, packet.payloadLength - 2);
    if (length < TEXT_HEADER_SIZE) {
        return false;
    }
    text->timestamp = readU32(plaintext);
    text->flags = plaintext[4];
    text->text = textFromPlaintext(plaintext + TEXT_HEADER_SIZE, length - TEXT_HEADER_SIZE);
    *ackHash = ackHashOf(plaintext, TEXT_HEADER_SIZE + text->text.size(), peerPublicKey);
    return true;
}

void meshcore_build_ack(uint32_t ackHash, MeshCorePacket* packet) {
    *packet = {};
    packet->route = MeshCoreRoute::Flood;
    packet->payloadType = MeshCorePayload::Ack;
    writeU32(packet->payload, ackHash);
    packet->payloadLength = 4;
}

bool meshcore_parse_ack(const MeshCorePacket& packet, uint32_t* ackHash) {
    if (packet.payloadType != MeshCorePayload::Ack || packet.payloadLength < 4) {
        return false;
    }
    *ackHash = readU32(packet.payload);
    return true;
}

bool meshcore_build_contact_uri(const char* name, const uint8_t publicKey[MESHCORE_PUBLIC_KEY_SIZE], uint8_t type, char* uri, size_t uriSize) {
    size_t length = 0;
    const auto append = [&](char character) {
        if (length + 1 >= uriSize) {
            return false;
        }
        uri[length++] = character;
        return true;
    };
    const auto appendText = [&](const char* text) {
        for (; *text != '\0'; text++) {
            if (!append(*text)) {
                return false;
            }
        }
        return true;
    };

    static const char HEX_DIGITS[] = "0123456789abcdef";
    if (!appendText("meshcore://contact/add?name=")) {
        return false;
    }
    // Percent-encoded, with a plus for a space like the MeshCore app does
    for (const char* character = name; *character != '\0'; character++) {
        const auto byte = static_cast<uint8_t>(*character);
        const bool unreserved = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
            byte == '-' || byte == '.' || byte == '_' || byte == '~';
        const bool ok = unreserved ? append(*character)
            : (byte == ' ' ? append('+') : (append('%') && append(HEX_DIGITS[byte >> 4]) && append(HEX_DIGITS[byte & 0x0F])));
        if (!ok) {
            return false;
        }
    }
    if (!appendText("&public_key=")) {
        return false;
    }
    for (size_t i = 0; i < MESHCORE_PUBLIC_KEY_SIZE; i++) {
        if (!append(HEX_DIGITS[publicKey[i] >> 4]) || !append(HEX_DIGITS[publicKey[i] & 0x0F])) {
            return false;
        }
    }
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "&type=%u", (unsigned)type);
    if (!appendText(suffix)) {
        return false;
    }
    uri[length] = '\0';
    return true;
}
