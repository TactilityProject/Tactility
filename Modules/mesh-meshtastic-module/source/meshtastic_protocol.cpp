// SPDX-License-Identifier: GPL-3.0-only
#include <mesh_meshtastic/meshtastic_protocol.h>

#include <psa/crypto.h>

#include <cstdio>
#include <cstring>

namespace {

// Protobuf wire types
constexpr uint8_t WIRE_VARINT = 0;
constexpr uint8_t WIRE_FIXED64 = 1;
constexpr uint8_t WIRE_LENGTH_DELIMITED = 2;
constexpr uint8_t WIRE_FIXED32 = 5;

void writeVarint(tt::std::vector<uint8_t>& out, uint64_t value) {
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        out.push_back(byte);
    } while (value != 0);
}

void writeTag(tt::std::vector<uint8_t>& out, uint32_t field, uint8_t wireType) {
    writeVarint(out, ((uint64_t)field << 3) | wireType);
}

void writeBytes(tt::std::vector<uint8_t>& out, uint32_t field, const uint8_t* bytes, size_t length) {
    writeTag(out, field, WIRE_LENGTH_DELIMITED);
    writeVarint(out, length);
    out.insert(out.end(), bytes, bytes + length);
}

void writeString(tt::std::vector<uint8_t>& out, uint32_t field, const tt::std::string& value) {
    writeBytes(out, field, reinterpret_cast<const uint8_t*>(value.data()), value.size());
}

void writeFixed32(tt::std::vector<uint8_t>& out, uint32_t field, uint32_t value) {
    writeTag(out, field, WIRE_FIXED32);
    for (int i = 0; i < 4; i++) {
        out.push_back((uint8_t)(value >> (8 * i)));
    }
}

class ProtobufReader {
    const uint8_t* data;
    size_t length;
    size_t position = 0;

public:
    ProtobufReader(const uint8_t* data, size_t length) : data(data), length(length) {}

    bool atEnd() const { return position >= length; }

    bool readVarint(uint64_t* value) {
        *value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            if (position >= length) {
                return false;
            }
            const uint8_t byte = data[position++];
            *value |= (uint64_t)(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }

    bool readField(uint32_t* field, uint8_t* wireType) {
        uint64_t tag;
        if (!readVarint(&tag) || (tag >> 3) == 0) {
            return false;
        }
        *field = (uint32_t)(tag >> 3);
        *wireType = (uint8_t)(tag & 0x07);
        return true;
    }

    bool readBytes(const uint8_t** bytes, size_t* bytesLength) {
        uint64_t size;
        if (!readVarint(&size) || size > length - position) {
            return false;
        }
        *bytes = data + position;
        *bytesLength = (size_t)size;
        position += (size_t)size;
        return true;
    }

    bool readFixed32(uint32_t* value) {
        if (position + 4 > length) {
            return false;
        }
        *value = data[position] | (data[position + 1] << 8) | (data[position + 2] << 16) | ((uint32_t)data[position + 3] << 24);
        position += 4;
        return true;
    }

    bool skip(uint8_t wireType) {
        uint64_t ignored;
        const uint8_t* bytes;
        size_t bytesLength;
        switch (wireType) {
            case WIRE_VARINT:
                return readVarint(&ignored);
            case WIRE_FIXED64:
                if (position + 8 > length) return false;
                position += 8;
                return true;
            case WIRE_LENGTH_DELIMITED:
                return readBytes(&bytes, &bytesLength);
            case WIRE_FIXED32:
                if (position + 4 > length) return false;
                position += 4;
                return true;
            default:
                return false;
        }
    }
};

} // namespace

const uint8_t MESHTASTIC_DEFAULT_KEY[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59, 0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

bool meshtastic_expand_psk(const uint8_t* psk, size_t length, MeshtasticChannelKey* key) {
    if (length == 1) {
        if (psk[0] == 0 || psk[0] > 10) {
            return false;
        }
        memcpy(key->bytes, MESHTASTIC_DEFAULT_KEY, 16);
        key->bytes[15] = (uint8_t)(key->bytes[15] + psk[0] - 1);
        key->length = 16;
        return true;
    }
    if (length == 16 || length == 32) {
        memcpy(key->bytes, psk, length);
        key->length = (uint8_t)length;
        return true;
    }
    return false;
}

uint8_t meshtastic_channel_hash(const char* name, const MeshtasticChannelKey& key) {
    uint8_t hash = 0;
    for (const char* c = name; *c != '\0'; c++) {
        hash ^= (uint8_t)*c;
    }
    for (size_t i = 0; i < key.length; i++) {
        hash ^= key.bytes[i];
    }
    return hash;
}

int32_t meshtastic_channel_frequency(const char* name, int32_t bandStart, int32_t bandEnd, int32_t bandwidth) {
    uint32_t hash = 5381;
    for (const char* c = name; *c != '\0'; c++) {
        hash = (hash << 5) + hash + (uint8_t)*c;
    }
    const int32_t channelCount = (bandEnd - bandStart) / bandwidth;
    const int32_t slot = (channelCount > 0) ? (int32_t)(hash % (uint32_t)channelCount) : 0;
    return bandStart + bandwidth / 2 + slot * bandwidth;
}

void meshtastic_write_header(const MeshtasticHeader& header, uint8_t data[MESHTASTIC_HEADER_SIZE]) {
    const uint32_t words[3] = { header.to, header.from, header.id };
    for (int word = 0; word < 3; word++) {
        for (int i = 0; i < 4; i++) {
            data[word * 4 + i] = (uint8_t)(words[word] >> (8 * i));
        }
    }
    data[12] = (uint8_t)((header.hopLimit & 0x07) | (header.wantAck ? 0x08 : 0) | (header.viaMqtt ? 0x10 : 0) | ((header.hopStart & 0x07) << 5));
    data[13] = header.channelHash;
    data[14] = header.nextHop;
    data[15] = header.relayNode;
}

bool meshtastic_read_header(const uint8_t* data, size_t length, MeshtasticHeader* header) {
    if (length < MESHTASTIC_HEADER_SIZE) {
        return false;
    }
    auto readU32 = [data](int offset) {
        return data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
    };
    header->to = readU32(0);
    header->from = readU32(4);
    header->id = readU32(8);
    header->hopLimit = data[12] & 0x07;
    header->wantAck = (data[12] & 0x08) != 0;
    header->viaMqtt = (data[12] & 0x10) != 0;
    header->hopStart = (data[12] >> 5) & 0x07;
    header->channelHash = data[13];
    header->nextHop = data[14];
    header->relayNode = data[15];
    return true;
}

bool meshtastic_crypt(const MeshtasticChannelKey& key, uint32_t packetId, uint32_t from, const uint8_t* input, uint8_t* output, size_t length) {
    // Nonce: packet id as 64-bit little-endian, sender as 32-bit little-endian, then a zero block counter
    uint8_t nonce[16] = {};
    for (int i = 0; i < 4; i++) {
        nonce[i] = (uint8_t)(packetId >> (8 * i));
        nonce[8 + i] = (uint8_t)(from >> (8 * i));
    }

    psa_crypto_init();
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_algorithm(&attributes, PSA_ALG_CTR);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
    psa_key_id_t id = PSA_KEY_ID_NULL;
    if (psa_import_key(&attributes, key.bytes, key.length, &id) != PSA_SUCCESS) {
        return false;
    }

    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    size_t written = 0;
    size_t finished = 0;
    const bool ok = psa_cipher_encrypt_setup(&operation, id, PSA_ALG_CTR) == PSA_SUCCESS &&
        psa_cipher_set_iv(&operation, nonce, sizeof(nonce)) == PSA_SUCCESS &&
        psa_cipher_update(&operation, input, length, output, length, &written) == PSA_SUCCESS &&
        psa_cipher_finish(&operation, output + written, length - written, &finished) == PSA_SUCCESS;
    psa_cipher_abort(&operation);
    psa_destroy_key(id);
    return ok && written + finished == length;
}

namespace {

constexpr psa_algorithm_t PKI_AEAD_ALGORITHM = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 8);

psa_key_id_t importPrivateKey(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE]) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 255);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    psa_key_id_t id = PSA_KEY_ID_NULL;
    if (psa_import_key(&attributes, privateKey, MESHTASTIC_PUBLIC_KEY_SIZE, &id) != PSA_SUCCESS) {
        return PSA_KEY_ID_NULL;
    }
    return id;
}

/** The AES-256 key: the SHA-256 of the X25519 shared secret, like Meshtastic derives it */
bool deriveSharedKey(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], const uint8_t peerPublicKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint8_t sharedKey[32]) {
    psa_crypto_init();
    const psa_key_id_t id = importPrivateKey(privateKey);
    if (id == PSA_KEY_ID_NULL) {
        return false;
    }
    uint8_t secret[32];
    size_t secretLength = 0;
    bool ok = psa_raw_key_agreement(PSA_ALG_ECDH, id, peerPublicKey, MESHTASTIC_PUBLIC_KEY_SIZE, secret, sizeof(secret), &secretLength) == PSA_SUCCESS && secretLength == sizeof(secret);
    psa_destroy_key(id);
    if (ok) {
        size_t hashLength = 0;
        ok = psa_hash_compute(PSA_ALG_SHA_256, secret, sizeof(secret), sharedKey, 32, &hashLength) == PSA_SUCCESS && hashLength == 32;
    }
    memset(secret, 0, sizeof(secret));
    return ok;
}

/** 13 bytes: packet id (4), extra nonce (4), sender (4), 0. The extra nonce replaces the upper half of the 64-bit packet id. */
void makePkiNonce(uint32_t packetId, uint32_t from, uint32_t extraNonce, uint8_t nonce[13]) {
    memset(nonce, 0, 13);
    for (int i = 0; i < 4; i++) {
        nonce[i] = (uint8_t)(packetId >> (8 * i));
        nonce[4 + i] = (uint8_t)(extraNonce >> (8 * i));
        nonce[8 + i] = (uint8_t)(from >> (8 * i));
    }
}

bool importAesKey(const uint8_t sharedKey[32], psa_key_usage_t usage, psa_key_id_t* id) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_algorithm(&attributes, PKI_AEAD_ALGORITHM);
    psa_set_key_usage_flags(&attributes, usage);
    return psa_import_key(&attributes, sharedKey, 32, id) == PSA_SUCCESS;
}

} // namespace

bool meshtastic_generate_key_pair(MeshtasticKeyPair* keyPair) {
    psa_crypto_init();
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 255);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    psa_key_id_t id = PSA_KEY_ID_NULL;
    if (psa_generate_key(&attributes, &id) != PSA_SUCCESS) {
        return false;
    }
    size_t privateLength = 0;
    size_t publicLength = 0;
    const bool ok = psa_export_key(id, keyPair->privateKey, sizeof(keyPair->privateKey), &privateLength) == PSA_SUCCESS &&
        privateLength == MESHTASTIC_PUBLIC_KEY_SIZE &&
        psa_export_public_key(id, keyPair->publicKey, sizeof(keyPair->publicKey), &publicLength) == PSA_SUCCESS &&
        publicLength == MESHTASTIC_PUBLIC_KEY_SIZE;
    psa_destroy_key(id);
    return ok;
}

bool meshtastic_public_key_from_private(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint8_t publicKey[MESHTASTIC_PUBLIC_KEY_SIZE]) {
    psa_crypto_init();
    const psa_key_id_t id = importPrivateKey(privateKey);
    if (id == PSA_KEY_ID_NULL) {
        return false;
    }
    size_t length = 0;
    const bool ok = psa_export_public_key(id, publicKey, MESHTASTIC_PUBLIC_KEY_SIZE, &length) == PSA_SUCCESS && length == MESHTASTIC_PUBLIC_KEY_SIZE;
    psa_destroy_key(id);
    return ok;
}

bool meshtastic_pki_encrypt(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], const uint8_t peerPublicKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint32_t packetId, uint32_t from, uint32_t extraNonce, const uint8_t* input, size_t length, uint8_t* output) {
    uint8_t sharedKey[32];
    if (extraNonce == 0 || !deriveSharedKey(privateKey, peerPublicKey, sharedKey)) {
        return false;
    }
    psa_key_id_t id = PSA_KEY_ID_NULL;
    const bool imported = importAesKey(sharedKey, PSA_KEY_USAGE_ENCRYPT, &id);
    memset(sharedKey, 0, sizeof(sharedKey));
    if (!imported) {
        return false;
    }
    uint8_t nonce[13];
    makePkiNonce(packetId, from, extraNonce, nonce);
    // The output is the ciphertext followed by the 8-byte tag
    size_t written = 0;
    const bool ok = psa_aead_encrypt(id, PKI_AEAD_ALGORITHM, nonce, sizeof(nonce), nullptr, 0, input, length, output, length + 8, &written) == PSA_SUCCESS && written == length + 8;
    psa_destroy_key(id);
    if (ok) {
        for (int i = 0; i < 4; i++) {
            output[length + 8 + i] = (uint8_t)(extraNonce >> (8 * i));
        }
    }
    return ok;
}

bool meshtastic_pki_decrypt(const uint8_t privateKey[MESHTASTIC_PUBLIC_KEY_SIZE], const uint8_t peerPublicKey[MESHTASTIC_PUBLIC_KEY_SIZE], uint32_t packetId, uint32_t from, const uint8_t* input, size_t length, uint8_t* output) {
    if (length <= MESHTASTIC_PKI_OVERHEAD) {
        return false;
    }
    const size_t payloadLength = length - MESHTASTIC_PKI_OVERHEAD;
    const uint8_t* extra = input + payloadLength + 8;
    const uint32_t extraNonce = extra[0] | (extra[1] << 8) | (extra[2] << 16) | ((uint32_t)extra[3] << 24);
    uint8_t sharedKey[32];
    if (!deriveSharedKey(privateKey, peerPublicKey, sharedKey)) {
        return false;
    }
    psa_key_id_t id = PSA_KEY_ID_NULL;
    const bool imported = importAesKey(sharedKey, PSA_KEY_USAGE_DECRYPT, &id);
    memset(sharedKey, 0, sizeof(sharedKey));
    if (!imported) {
        return false;
    }
    uint8_t nonce[13];
    makePkiNonce(packetId, from, extraNonce, nonce);
    size_t written = 0;
    const bool ok = psa_aead_decrypt(id, PKI_AEAD_ALGORITHM, nonce, sizeof(nonce), nullptr, 0, input, payloadLength + 8, output, payloadLength, &written) == PSA_SUCCESS && written == payloadLength;
    psa_destroy_key(id);
    return ok;
}

tt::std::vector<uint8_t> meshtastic_encode_data(const MeshtasticData& data) {
    tt::std::vector<uint8_t> out;
    writeTag(out, 1, WIRE_VARINT);
    writeVarint(out, (uint32_t)data.port);
    writeBytes(out, 2, data.payload.data(), data.payload.size());
    if (data.wantResponse) {
        writeTag(out, 3, WIRE_VARINT);
        writeVarint(out, 1);
    }
    if (data.requestId != 0) {
        writeFixed32(out, 6, data.requestId);
    }
    return out;
}

bool meshtastic_decode_data(const uint8_t* bytes, size_t length, MeshtasticData* data) {
    ProtobufReader reader(bytes, length);
    *data = {};
    bool hasPort = false;
    while (!reader.atEnd()) {
        uint32_t field;
        uint8_t wireType;
        if (!reader.readField(&field, &wireType)) {
            return false;
        }
        uint64_t value;
        const uint8_t* fieldBytes;
        size_t fieldLength;
        if (field == 1 && wireType == WIRE_VARINT) {
            if (!reader.readVarint(&value)) return false;
            data->port = (MeshtasticPort)value;
            hasPort = true;
        } else if (field == 2 && wireType == WIRE_LENGTH_DELIMITED) {
            if (!reader.readBytes(&fieldBytes, &fieldLength)) return false;
            data->payload.assign(fieldBytes, fieldBytes + fieldLength);
        } else if (field == 3 && wireType == WIRE_VARINT) {
            if (!reader.readVarint(&value)) return false;
            data->wantResponse = value != 0;
        } else if (field == 6 && wireType == WIRE_FIXED32) {
            if (!reader.readFixed32(&data->requestId)) return false;
        } else if (!reader.skip(wireType)) {
            return false;
        }
    }
    return hasPort;
}

tt::std::vector<uint8_t> meshtastic_encode_user(const MeshtasticUser& user) {
    tt::std::vector<uint8_t> out;
    writeString(out, 1, user.id);
    writeString(out, 2, user.longName);
    writeString(out, 3, user.shortName);
    // hw_model PRIVATE_HW
    writeTag(out, 5, WIRE_VARINT);
    writeVarint(out, 255);
    if (user.publicKey.size() == MESHTASTIC_PUBLIC_KEY_SIZE) {
        writeBytes(out, 8, user.publicKey.data(), user.publicKey.size());
    }
    return out;
}

bool meshtastic_decode_user(const uint8_t* bytes, size_t length, MeshtasticUser* user) {
    ProtobufReader reader(bytes, length);
    *user = {};
    while (!reader.atEnd()) {
        uint32_t field;
        uint8_t wireType;
        if (!reader.readField(&field, &wireType)) {
            return false;
        }
        const uint8_t* fieldBytes;
        size_t fieldLength;
        if (field >= 1 && field <= 3 && wireType == WIRE_LENGTH_DELIMITED) {
            if (!reader.readBytes(&fieldBytes, &fieldLength)) return false;
            tt::std::string value(reinterpret_cast<const char*>(fieldBytes), fieldLength);
            if (field == 1) user->id = value;
            else if (field == 2) user->longName = value;
            else user->shortName = value;
        } else if (field == 8 && wireType == WIRE_LENGTH_DELIMITED) {
            if (!reader.readBytes(&fieldBytes, &fieldLength)) return false;
            if (fieldLength == MESHTASTIC_PUBLIC_KEY_SIZE) {
                user->publicKey.assign(fieldBytes, fieldBytes + fieldLength);
            }
        } else if (!reader.skip(wireType)) {
            return false;
        }
    }
    return true;
}

tt::std::vector<uint8_t> meshtastic_encode_routing_ack() {
    // Routing { error_reason: NONE }
    tt::std::vector<uint8_t> out;
    writeTag(out, 3, WIRE_VARINT);
    writeVarint(out, 0);
    return out;
}

tt::std::string meshtastic_node_id(uint32_t nodeNum) {
    char id[12];
    snprintf(id, sizeof(id), "!%08x", (unsigned)nodeNum);
    return id;
}
