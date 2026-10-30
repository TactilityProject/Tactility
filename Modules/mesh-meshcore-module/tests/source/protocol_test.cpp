#include "doctest.h"

#include <mesh_meshcore/meshcore_backend.h>
#include <mesh_meshcore/meshcore_packet.h>
#include <mesh_meshcore/meshcore_protocol.h>

#include <cstdlib>
#include <cstring>
#include <string>

TEST_CASE("MeshCore packets survive a serialize and parse round trip") {
    MeshCorePacket packet;
    packet.route = MeshCoreRoute::TransportFlood;
    packet.payloadType = MeshCorePayload::GroupText;
    packet.transportCodes[0] = 0x1234;
    packet.transportCodes[1] = 0xABCD;
    packet.pathLength = 3;
    packet.path[0] = 0x11;
    packet.path[1] = 0x22;
    packet.path[2] = 0x33;
    packet.payloadLength = 5;
    memcpy(packet.payload, "hello", 5);

    uint8_t frame[255];
    const size_t length = meshcore_packet_serialize(packet, frame, sizeof(frame));
    CHECK_EQ(length, 1 + 4 + 1 + 3 + 5);
    // Route 0, payload type 5 in bits 2-5
    CHECK_EQ(frame[0], 0x14);
    CHECK_EQ(frame[5], 3);

    MeshCorePacket parsed;
    REQUIRE(meshcore_packet_parse(frame, length, &parsed));
    CHECK_EQ(parsed.route, MeshCoreRoute::TransportFlood);
    CHECK_EQ(parsed.payloadType, MeshCorePayload::GroupText);
    CHECK_EQ(parsed.transportCodes[1], 0xABCD);
    CHECK_EQ(parsed.pathLength, 3);
    CHECK_EQ(parsed.path[2], 0x33);
    CHECK_EQ(parsed.payloadLength, 5);
    CHECK_EQ(memcmp(parsed.payload, "hello", 5), 0);
    CHECK_EQ(meshcore_packet_hash(parsed), meshcore_packet_hash(packet));
}

TEST_CASE("MeshCore parser rejects truncated frames") {
    const uint8_t header_only[] = { 0x15 };
    MeshCorePacket packet;
    CHECK_FALSE(meshcore_packet_parse(header_only, sizeof(header_only), &packet));
    // Path length claims 10 hops but only 2 bytes follow
    const uint8_t short_path[] = { 0x15, 10, 0x01, 0x02 };
    CHECK_FALSE(meshcore_packet_parse(short_path, sizeof(short_path), &packet));
}

TEST_CASE("MeshCore public channel hash matches the well-known value") {
    MeshCoreChannel channel;
    meshcore_channel_from_key(MESHCORE_PUBLIC_CHANNEL_KEY, &channel);
    // The public channel is addressed with hash 0x11
    CHECK_EQ(channel.hash, 0x11);
}

TEST_CASE("MeshCore hashtag channel keys are derived from the name") {
    uint8_t key[16];
    uint8_t again[16];
    meshcore_hashtag_channel_key("#test", key);
    meshcore_hashtag_channel_key("#test", again);
    CHECK(memcmp(key, again, sizeof(key)) == 0);
    meshcore_hashtag_channel_key("#other", again);
    CHECK(memcmp(key, again, sizeof(key)) != 0);
}

TEST_CASE("MeshCore group text round trip") {
    MeshCoreChannel channel;
    meshcore_channel_from_key(MESHCORE_PUBLIC_CHANNEL_KEY, &channel);

    MeshCorePacket packet;
    REQUIRE(meshcore_build_group_text(channel, 1750000000, "alice", "hi there", &packet));
    CHECK_EQ(packet.payload[0], channel.hash);

    MeshCoreText text;
    REQUIRE(meshcore_parse_group_text(channel, packet, &text));
    CHECK_EQ(text.timestamp, 1750000000);
    CHECK_EQ(text.text, "alice: hi there");

    // Another key fails the MAC
    uint8_t otherKey[16] = { 1 };
    MeshCoreChannel other;
    meshcore_channel_from_key(otherKey, &other);
    other.hash = channel.hash;
    CHECK_FALSE(meshcore_parse_group_text(other, packet, &text));
}

TEST_CASE("MeshCore adverts are signed and verified") {
    MeshCoreIdentity identity;
    meshcore_identity_create(&identity);

    MeshCorePacket packet;
    REQUIRE(meshcore_build_advert(identity, 1234, "Tactility", &packet));

    MeshCoreAdvert advert;
    REQUIRE(meshcore_parse_advert(packet, &advert));
    CHECK_EQ(advert.timestamp, 1234);
    CHECK_EQ(advert.name, "Tactility");
    CHECK_EQ(advert.type, MeshCoreNodeType::Chat);
    CHECK_EQ(memcmp(advert.publicKey, identity.publicKey, 32), 0);

    // Tampering breaks the signature
    packet.payload[packet.payloadLength - 1] ^= 0x01;
    CHECK_FALSE(meshcore_parse_advert(packet, &advert));
}

TEST_CASE("MeshCore direct messages decrypt at the recipient and produce matching ACK hashes") {
    MeshCoreIdentity alice;
    MeshCoreIdentity bob;
    meshcore_identity_create(&alice);
    meshcore_identity_create(&bob);

    uint8_t aliceSecret[32];
    uint8_t bobSecret[32];
    meshcore_shared_secret(alice, bob.publicKey, aliceSecret);
    meshcore_shared_secret(bob, alice.publicKey, bobSecret);
    CHECK_EQ(memcmp(aliceSecret, bobSecret, 32), 0);

    MeshCorePacket packet;
    uint32_t sentAckHash = 0;
    REQUIRE(meshcore_build_text_message(alice, bob.publicKey, 42, 0, "secret", &packet, &sentAckHash));

    uint8_t destinationHash;
    uint8_t sourceHash;
    REQUIRE(meshcore_peek_text_message(packet, &destinationHash, &sourceHash));
    CHECK_EQ(destinationHash, bob.publicKey[0]);
    CHECK_EQ(sourceHash, alice.publicKey[0]);

    MeshCoreText text;
    uint32_t receivedAckHash = 0;
    REQUIRE(meshcore_parse_text_message(bob, alice.publicKey, packet, &text, &receivedAckHash));
    CHECK_EQ(text.text, "secret");
    CHECK_EQ(text.timestamp, 42);
    CHECK_EQ(receivedAckHash, sentAckHash);

    MeshCorePacket ack;
    meshcore_build_ack(receivedAckHash, &ack);
    uint32_t parsedAckHash = 0;
    REQUIRE(meshcore_parse_ack(ack, &parsedAckHash));
    CHECK_EQ(parsedAckHash, sentAckHash);
}

TEST_CASE("MeshCore contact URIs match the format of the MeshCore app") {
    uint8_t key[MESHCORE_PUBLIC_KEY_SIZE];
    const char* hex = "9cd8fcf22a47333b591d96a2b848b73f457b1bb1a3ea2453a885f9e5787765b1";
    for (size_t i = 0; i < sizeof(key); i++) {
        char pair[3] = { hex[i * 2], hex[i * 2 + 1], '\0' };
        key[i] = (uint8_t)strtoul(pair, nullptr, 16);
    }

    char uri[256];
    REQUIRE(meshcore_build_contact_uri("Example Contact", key, 1, uri, sizeof(uri)));
    CHECK_EQ(std::string(uri), "meshcore://contact/add?name=Example+Contact&public_key=9cd8fcf22a47333b591d96a2b848b73f457b1bb1a3ea2453a885f9e5787765b1&type=1");

    REQUIRE(meshcore_build_contact_uri("Ken's deck&co", key, 2, uri, sizeof(uri)));
    CHECK_EQ(std::string(uri).substr(0, 52), "meshcore://contact/add?name=Ken%27s+deck%26co&public");

    // Too small for the URI
    CHECK_FALSE(meshcore_build_contact_uri("Example Contact", key, 1, uri, 60));
}

TEST_CASE("a MeshCore preset needs the region that allows its frequency") {
    const auto presetIndex = [](const char* name) {
        for (size_t i = 0; meshcore_backend_api.presets[i] != nullptr; i++) {
            if (strcmp(meshcore_backend_api.presets[i], name) == 0) {
                return i;
            }
        }
        FAIL("unknown preset");
        return size_t(0);
    };
    RadioRegion region = RADIO_REGION_UNSET;

    CHECK_EQ(meshcore_backend_api.get_preset_region(presetIndex("Netherlands"), RADIO_REGION_US_915, &region), ERROR_NONE);
    CHECK_EQ(region, RADIO_REGION_EU_868);
    CHECK_EQ(meshcore_backend_api.get_preset_region(presetIndex("Australia"), RADIO_REGION_EU_868, &region), ERROR_NONE);
    CHECK_EQ(region, RADIO_REGION_AU_915);
    CHECK_EQ(meshcore_backend_api.get_preset_region(presetIndex("USA"), RADIO_REGION_US_915, &region), ERROR_NONE);
    CHECK_EQ(region, RADIO_REGION_US_915);
    CHECK_EQ(meshcore_backend_api.get_preset_region(presetIndex("EU 433MHz (Narrow)"), RADIO_REGION_EU_868, &region), ERROR_NONE);
    CHECK_EQ(region, RADIO_REGION_EU_433);
    CHECK_EQ(meshcore_backend_api.get_preset_region(presetIndex("Default"), RADIO_REGION_JP_920, &region), ERROR_NONE);
    CHECK_EQ(region, RADIO_REGION_JP_920);
}

TEST_CASE("a region scope limits a packet with a code that other MeshCore nodes compute the same way") {
    // The key and codes were computed with an independent HMAC-SHA256
    uint8_t key[16];
    meshcore_scope_key("nl", key);
    const uint8_t expected[16] = { 0xa8, 0x28, 0x9f, 0x73, 0x66, 0x5c, 0x28, 0x05, 0x7d, 0x4f, 0x38, 0x15, 0xa6, 0x00, 0x29, 0x21 };
    CHECK_EQ(memcmp(key, expected, sizeof(key)), 0);
    uint8_t hashtagKey[16];
    meshcore_scope_key("#nl", hashtagKey);
    CHECK_EQ(memcmp(key, hashtagKey, sizeof(key)), 0);

    MeshCorePacket packet;
    packet.payloadType = MeshCorePayload::TextMessage;
    packet.payloadLength = 5;
    memcpy(packet.payload, "hello", 5);
    uint16_t code = 0;
    REQUIRE(meshcore_transport_code(key, packet, &code));
    CHECK_EQ(code, 0xEEC8);

    packet.payloadType = MeshCorePayload::Advert;
    packet.payloadLength = 0;
    REQUIRE(meshcore_transport_code(key, packet, &code));
    CHECK_EQ(code, 0xF329);
}

TEST_CASE("a MeshCore preset only gives a radio profile in a region that allows its frequency") {
    size_t netherlands = 0;
    while (strcmp(meshcore_backend_api.presets[netherlands], "Netherlands") != 0) {
        netherlands++;
    }
    MeshRadioProfile profile {};
    CHECK_EQ(meshcore_backend_api.get_radio_profile(nullptr, RADIO_REGION_US_915, RADIO_MODULATION_LORA, netherlands, &profile), ERROR_NOT_SUPPORTED);
    CHECK_EQ(meshcore_backend_api.get_radio_profile(nullptr, RADIO_REGION_EU_868, RADIO_MODULATION_LORA, netherlands, &profile), ERROR_NONE);
    CHECK_EQ(profile.frequency, 869618000);
}

TEST_CASE("HMAC-SHA256 matches the RFC 4231 test vector") {
    const uint8_t key[] = { 'J', 'e', 'f', 'e' };
    const char* message = "what do ya want for nothing?";
    const uint8_t expected[32] = {
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24, 0x26, 0x08, 0x95, 0x75, 0xc7,
        0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27, 0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43,
    };
    uint8_t mac[32];
    REQUIRE(meshcore_hmac_sha256(key, sizeof(key), reinterpret_cast<const uint8_t*>(message), strlen(message), mac));
    CHECK_EQ(memcmp(mac, expected, sizeof(mac)), 0);
}

TEST_CASE("every MeshCore preset can be used in the region that it selects") {
    for (size_t preset = 0; meshcore_backend_api.presets[preset] != nullptr; preset++) {
        CAPTURE(std::string(meshcore_backend_api.presets[preset]));
        // Whatever region the radio has, a preset ends up in a region that gives it a profile that lies in the region's bands
        for (int current = RADIO_REGION_EU_433; current < RADIO_REGION_COUNT; current++) {
            RadioRegion region = RADIO_REGION_UNSET;
            if (meshcore_backend_api.get_preset_region(preset, static_cast<RadioRegion>(current), &region) != ERROR_NONE) {
                FAIL("no region allows the preset");
            }
            MeshRadioProfile profile {};
            if (meshcore_backend_api.get_radio_profile(nullptr, region, RADIO_MODULATION_LORA, preset, &profile) != ERROR_NONE) {
                // The default preset only exists in some regions
                CHECK_EQ(preset, 0);
                continue;
            }
            const RadioRegionBand* bands = nullptr;
            const size_t count = radio_get_region_bands(region, &bands);
            bool fits = false;
            for (size_t i = 0; i < count; i++) {
                fits |= profile.frequency - profile.bandwidth / 2 >= bands[i].frequency_min && profile.frequency + profile.bandwidth / 2 <= bands[i].frequency_max;
            }
            CHECK(fits);
        }
    }
}
