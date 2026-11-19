#include "doctest.h"

#include <mesh_meshtastic/meshtastic_backend.h>
#include <mesh_meshtastic/meshtastic_protocol.h>

#include <tactility/drivers/radio.h>

#include <cstring>
#include <string>

namespace {

MeshtasticChannelKey defaultKey() {
    const uint8_t psk = 1;
    MeshtasticChannelKey key {};
    REQUIRE(meshtastic_expand_psk(&psk, 1, &key));
    return key;
}

} // namespace

TEST_CASE("Meshtastic default channel matches the known LongFast values") {
    const auto key = defaultKey();
    CHECK_EQ(key.length, 16);
    CHECK_EQ(memcmp(key.bytes, MESHTASTIC_DEFAULT_KEY, 16), 0);
    // LongFast with the default key is channel hash 8
    CHECK_EQ(meshtastic_channel_hash("LongFast", key), 0x08);
    // US LongFast sits at 906.875 MHz, EU_868 has a single slot
    CHECK_EQ(meshtastic_channel_frequency("LongFast", 902000000, 928000000, 250000), 906875000);
    CHECK_EQ(meshtastic_channel_frequency("LongFast", 869400000, 869650000, 250000), 869525000);
}

TEST_CASE("Meshtastic PSK expansion") {
    const uint8_t second = 2;
    MeshtasticChannelKey key {};
    REQUIRE(meshtastic_expand_psk(&second, 1, &key));
    CHECK_EQ(key.bytes[15], MESHTASTIC_DEFAULT_KEY[15] + 1);

    const uint8_t none = 0;
    CHECK_FALSE(meshtastic_expand_psk(&none, 1, &key));
    uint8_t long_key[32] = {};
    CHECK(meshtastic_expand_psk(long_key, 32, &key));
    CHECK_EQ(key.length, 32);
    CHECK_FALSE(meshtastic_expand_psk(long_key, 20, &key));
}

TEST_CASE("Meshtastic header round trip") {
    MeshtasticHeader header;
    header.to = 0x11223344;
    header.from = 0xAABBCCDD;
    header.id = 0x01020304;
    header.hopLimit = 2;
    header.wantAck = true;
    header.hopStart = 3;
    header.channelHash = 8;

    uint8_t data[MESHTASTIC_HEADER_SIZE];
    meshtastic_write_header(header, data);
    CHECK_EQ(data[0], 0x44);
    CHECK_EQ(data[7], 0xAA);
    CHECK_EQ(data[12], 2 | 0x08 | (3 << 5));

    MeshtasticHeader parsed;
    REQUIRE(meshtastic_read_header(data, sizeof(data), &parsed));
    CHECK_EQ(parsed.to, header.to);
    CHECK_EQ(parsed.from, header.from);
    CHECK_EQ(parsed.id, header.id);
    CHECK_EQ(parsed.hopLimit, 2);
    CHECK(parsed.wantAck);
    CHECK_EQ(parsed.hopStart, 3);
    CHECK_EQ(parsed.channelHash, 8);
}

TEST_CASE("Meshtastic Data encoding and AES-CTR round trip") {
    MeshtasticData data;
    data.port = MeshtasticPort::TextMessage;
    const char* text = "hello mesh";
    data.payload.assign(text, text + strlen(text));
    data.requestId = 0x12345678;

    const auto encoded = meshtastic_encode_data(data);
    // portnum = 1, then the payload
    CHECK_EQ(encoded[0], 0x08);
    CHECK_EQ(encoded[1], 0x01);
    CHECK_EQ(encoded[2], 0x12);
    CHECK_EQ(encoded[3], strlen(text));

    const auto key = defaultKey();
    uint8_t ciphertext[64];
    uint8_t plaintext[64];
    REQUIRE(meshtastic_crypt(key, 42, 0xAABBCCDD, encoded.data(), ciphertext, encoded.size()));
    CHECK_NE(memcmp(ciphertext, encoded.data(), encoded.size()), 0);
    REQUIRE(meshtastic_crypt(key, 42, 0xAABBCCDD, ciphertext, plaintext, encoded.size()));

    MeshtasticData decoded;
    REQUIRE(meshtastic_decode_data(plaintext, encoded.size(), &decoded));
    CHECK_EQ(decoded.port, MeshtasticPort::TextMessage);
    CHECK_EQ(std::string(decoded.payload.begin(), decoded.payload.end()), text);
    CHECK_EQ(decoded.requestId, 0x12345678);

    // Decrypting with another packet id yields garbage
    REQUIRE(meshtastic_crypt(key, 43, 0xAABBCCDD, ciphertext, plaintext, encoded.size()));
    CHECK_NE(memcmp(plaintext, encoded.data(), encoded.size()), 0);
}

TEST_CASE("Meshtastic User encoding round trip") {
    MeshtasticUser user;
    user.id = meshtastic_node_id(0xDEADBEEF);
    user.longName = "Tactility";
    user.shortName = "Tact";
    CHECK_EQ(user.id, "!deadbeef");

    const auto encoded = meshtastic_encode_user(user);
    MeshtasticUser decoded;
    REQUIRE(meshtastic_decode_user(encoded.data(), encoded.size(), &decoded));
    CHECK_EQ(decoded.id, "!deadbeef");
    CHECK_EQ(decoded.longName, "Tactility");
    CHECK_EQ(decoded.shortName, "Tact");
}

TEST_CASE("Meshtastic User carries a public key") {
    MeshtasticUser user;
    user.id = meshtastic_node_id(1);
    user.longName = "Tactility";
    user.publicKey.assign(32, 0x5A);
    const auto encoded = meshtastic_encode_user(user);
    MeshtasticUser decoded;
    REQUIRE(meshtastic_decode_user(encoded.data(), encoded.size(), &decoded));
    CHECK(decoded.publicKey == user.publicKey);
}

TEST_CASE("X25519 public key matches the RFC 7748 vector") {
    const uint8_t privateKey[32] = {
        0x77, 0x07, 0x6d, 0x0a, 0x73, 0x18, 0xa5, 0x7d, 0x3c, 0x16, 0xc1, 0x72, 0x51, 0xb2, 0x66, 0x45,
        0xdf, 0x4c, 0x2f, 0x87, 0xeb, 0xc0, 0x99, 0x2a, 0xb1, 0x77, 0xfb, 0xa5, 0x1d, 0xb9, 0x2c, 0x2a
    };
    const uint8_t expected[32] = {
        0x85, 0x20, 0xf0, 0x09, 0x89, 0x30, 0xa7, 0x54, 0x74, 0x8b, 0x7d, 0xdc, 0xb4, 0x3e, 0xf7, 0x5a,
        0x0d, 0xbf, 0x3a, 0x0d, 0x26, 0x38, 0x1a, 0xf4, 0xeb, 0xa4, 0xa9, 0x8e, 0xaa, 0x9b, 0x4e, 0x6a
    };
    uint8_t publicKey[32];
    REQUIRE(meshtastic_public_key_from_private(privateKey, publicKey));
    CHECK_EQ(memcmp(publicKey, expected, 32), 0);
}

TEST_CASE("Meshtastic public key encryption round trip") {
    MeshtasticKeyPair alice;
    MeshtasticKeyPair bob;
    REQUIRE(meshtastic_generate_key_pair(&alice));
    REQUIRE(meshtastic_generate_key_pair(&bob));

    MeshtasticData data;
    data.port = MeshtasticPort::TextMessage;
    const char text[] = "direct message";
    data.payload.assign(text, text + sizeof(text) - 1);
    const auto encoded = meshtastic_encode_data(data);

    uint8_t frame[256];
    REQUIRE(meshtastic_pki_encrypt(alice.privateKey, bob.publicKey, 0x1234, 0xAABBCCDD, 0x55667788, encoded.data(), encoded.size(), frame));

    uint8_t plaintext[256];
    REQUIRE(meshtastic_pki_decrypt(bob.privateKey, alice.publicKey, 0x1234, 0xAABBCCDD, frame, encoded.size() + MESHTASTIC_PKI_OVERHEAD, plaintext));
    CHECK_EQ(memcmp(plaintext, encoded.data(), encoded.size()), 0);

    // A different packet id, a wrong key or a changed byte fails the authentication
    CHECK_FALSE(meshtastic_pki_decrypt(bob.privateKey, alice.publicKey, 0x1235, 0xAABBCCDD, frame, encoded.size() + MESHTASTIC_PKI_OVERHEAD, plaintext));
    frame[0] ^= 1;
    CHECK_FALSE(meshtastic_pki_decrypt(bob.privateKey, alice.publicKey, 0x1234, 0xAABBCCDD, frame, encoded.size() + MESHTASTIC_PKI_OVERHEAD, plaintext));
}

TEST_CASE("a Meshtastic preset either fits the region or is refused") {
    size_t presetCount = 0;
    while (meshtastic_backend_api.presets[presetCount] != nullptr) {
        presetCount++;
    }
    CHECK_EQ(presetCount, 8);

    for (int region = RADIO_REGION_EU_433; region < RADIO_REGION_COUNT; region++) {
        // Meshtastic's 433 MHz band starts at 433.0 MHz, below the 433.05 MHz of the radio region, so its first slot doesn't fit
        if (region == RADIO_REGION_EU_433) {
            continue;
        }
        for (size_t preset = 0; preset < presetCount; preset++) {
            CAPTURE(std::string(radio_region_to_string(static_cast<RadioRegion>(region))));
            CAPTURE(std::string(meshtastic_backend_api.presets[preset]));
            MeshRadioProfile profile {};
            if (meshtastic_backend_api.get_radio_profile(nullptr, static_cast<RadioRegion>(region), RADIO_MODULATION_LORA, preset, &profile) != ERROR_NONE) {
                continue;
            }
            const RadioRegionBand* bands = nullptr;
            const size_t count = radio_get_region_bands(static_cast<RadioRegion>(region), &bands);
            bool fits = false;
            for (size_t i = 0; i < count; i++) {
                fits |= profile.frequency - profile.bandwidth / 2 >= bands[i].frequency_min && profile.frequency + profile.bandwidth / 2 <= bands[i].frequency_max;
            }
            CHECK(fits);
        }
    }
}

TEST_CASE("ShortTurbo is refused where its bandwidth exceeds the band") {
    size_t shortTurbo = 0;
    while (strcmp(meshtastic_backend_api.presets[shortTurbo], "ShortTurbo") != 0) {
        shortTurbo++;
    }
    MeshRadioProfile profile {};
    CHECK_EQ(meshtastic_backend_api.get_radio_profile(nullptr, RADIO_REGION_EU_868, RADIO_MODULATION_LORA, shortTurbo, &profile), ERROR_NOT_SUPPORTED);
    CHECK_EQ(meshtastic_backend_api.get_radio_profile(nullptr, RADIO_REGION_US_915, RADIO_MODULATION_LORA, shortTurbo, &profile), ERROR_NONE);
    CHECK_EQ(profile.bandwidth, 500000);
}
