#include "doctest.h"

#include <tactility/drivers/hid_consumer.h>

#include <cstring>

// Report descriptor captured from a "Generic AB13X USB Audio" headset (0020:0b21):
// Report ID 1, then eight 1-bit Consumer-page usages (Play/Pause, Vol+, Vol-, plus four more)
// and one 1-bit Telephony-page usage. This is the shape the parser must map.
static const uint8_t HEADSET_DESCRIPTOR[] = {
    0x05, 0x0C,       // Usage Page (Consumer)
    0x09, 0x01,       //   Usage (Consumer Control)
    0xA1, 0x01,       //   Collection (Application)
    0x85, 0x01,       //     Report ID (1)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x09, 0xCD,       //     Usage (Play/Pause)
    0x09, 0xE9,       //     Usage (Volume Increment)
    0x09, 0xEA,       //     Usage (Volume Decrement)
    0x09, 0xCF,       //     Usage (...)
    0x09, 0x40,       //     Usage (Menu)
    0x09, 0xB5,       //     Usage (Scan Next Track)
    0x09, 0xB6,       //     Usage (Scan Previous Track)
    0x09, 0xE2,       //     Usage (Mute)
    0x95, 0x08,       //     Report Count (8)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)
    0x05, 0x0B,       //     Usage Page (Telephony)
    0x95, 0x01,       //     Report Count (1)
    0x09, 0x21,       //     Usage (...)
    0x81, 0x02,       //     Input (Data, Variable, Absolute)
    0x95, 0x07,       //     Report Count (7)
    0x81, 0x05,       //     Input (Constant)
    0xC0              //   End Collection
};

TEST_CASE("hid_consumer_parse_descriptor maps a headset Consumer Control descriptor") {
    HidConsumerMap map;
    CHECK(hid_consumer_parse_descriptor(HEADSET_DESCRIPTOR, sizeof(HEADSET_DESCRIPTOR), &map));

    CHECK(map.has_report_id);
    CHECK_EQ(map.report_id, 1);
    CHECK_EQ(map.usage_count, 8);
    CHECK_EQ(map.payload_bytes, 1);

    // Bit order follows the descriptor's usage order.
    CHECK_EQ(map.usages[0].usage, 0xCD);
    CHECK_EQ(map.usages[0].bit_index, 0);
    CHECK_EQ(map.usages[1].usage, 0xE9);
    CHECK_EQ(map.usages[1].bit_index, 1);
    CHECK_EQ(map.usages[2].usage, 0xEA);
    CHECK_EQ(map.usages[2].bit_index, 2);
    CHECK_EQ(map.usages[7].usage, 0xE2);
    CHECK_EQ(map.usages[7].bit_index, 7);
}

TEST_CASE("hid_consumer_decode_report decodes headset button reports") {
    HidConsumerMap map;
    REQUIRE(hid_consumer_parse_descriptor(HEADSET_DESCRIPTOR, sizeof(HEADSET_DESCRIPTOR), &map));

    uint16_t usages[HID_CONSUMER_MAX_USAGES];

    // Captured press of the center (Play/Pause) button: report ID, bitmap, padding.
    const uint8_t play_pause_press[] = { 0x01, 0x01, 0x00 };
    size_t count = hid_consumer_decode_report(&map, play_pause_press, sizeof(play_pause_press), usages, HID_CONSUMER_MAX_USAGES);
    CHECK_EQ(count, 1);
    CHECK_EQ(usages[0], HID_CONSUMER_USAGE_PLAY_PAUSE);

    // Captured Volume+ and Volume- presses.
    const uint8_t volume_up_press[] = { 0x01, 0x02, 0x00 };
    count = hid_consumer_decode_report(&map, volume_up_press, sizeof(volume_up_press), usages, HID_CONSUMER_MAX_USAGES);
    CHECK_EQ(count, 1);
    CHECK_EQ(usages[0], HID_CONSUMER_USAGE_VOLUME_INCREMENT);

    const uint8_t volume_down_press[] = { 0x01, 0x04, 0x00 };
    count = hid_consumer_decode_report(&map, volume_down_press, sizeof(volume_down_press), usages, HID_CONSUMER_MAX_USAGES);
    CHECK_EQ(count, 1);
    CHECK_EQ(usages[0], HID_CONSUMER_USAGE_VOLUME_DECREMENT);

    // Release: no usages pressed.
    const uint8_t release[] = { 0x01, 0x00, 0x00 };
    count = hid_consumer_decode_report(&map, release, sizeof(release), usages, HID_CONSUMER_MAX_USAGES);
    CHECK_EQ(count, 0);

    // Multiple simultaneous presses decode together (bitmap 0x05 = Play/Pause + Vol-).
    const uint8_t multi_press[] = { 0x01, 0x05, 0x00 };
    count = hid_consumer_decode_report(&map, multi_press, sizeof(multi_press), usages, HID_CONSUMER_MAX_USAGES);
    CHECK_EQ(count, 2);
    CHECK_EQ(usages[0], HID_CONSUMER_USAGE_PLAY_PAUSE);
    CHECK_EQ(usages[1], HID_CONSUMER_USAGE_VOLUME_DECREMENT);

    // Wrong report ID and short reports decode to nothing.
    const uint8_t wrong_id[] = { 0x02, 0x01, 0x00 };
    CHECK_EQ(hid_consumer_decode_report(&map, wrong_id, sizeof(wrong_id), usages, HID_CONSUMER_MAX_USAGES), 0);
    const uint8_t too_short[] = { 0x01 };
    CHECK_EQ(hid_consumer_decode_report(&map, too_short, sizeof(too_short), usages, HID_CONSUMER_MAX_USAGES), 0);
}

TEST_CASE("hid_consumer_parse_descriptor handles descriptors without a report ID") {
    // Same shape, no Report ID item: usages are payload byte 0's bits.
    static const uint8_t no_id_descriptor[] = {
        0x05, 0x0C,       // Usage Page (Consumer)
        0x09, 0x01,       //   Usage (Consumer Control)
        0xA1, 0x01,       //   Collection (Application)
        0x09, 0xCD,       //     Usage (Play/Pause)
        0x09, 0xE9,       //     Usage (Volume Increment)
        0x95, 0x02,       //     Report Count (2)
        0x75, 0x01,       //     Report Size (1)
        0x81, 0x02,       //     Input (Data, Variable, Absolute)
        0xC0              //   End Collection
    };

    HidConsumerMap map;
    CHECK(hid_consumer_parse_descriptor(no_id_descriptor, sizeof(no_id_descriptor), &map));
    CHECK(!map.has_report_id);
    CHECK_EQ(map.usage_count, 2);

    uint16_t usages[HID_CONSUMER_MAX_USAGES];
    const uint8_t press[] = { 0x02 };
    CHECK_EQ(hid_consumer_decode_report(&map, press, sizeof(press), usages, HID_CONSUMER_MAX_USAGES), 1);
    CHECK_EQ(usages[0], HID_CONSUMER_USAGE_VOLUME_INCREMENT);
}

TEST_CASE("hid_consumer_parse_descriptor maps Usage Minimum/Maximum ranges") {
    // Some devices compress a button row into a usage range instead of listing each usage.
    static const uint8_t range_descriptor[] = {
        0x05, 0x0C,       // Usage Page (Consumer)
        0x09, 0x01,       //   Usage (Consumer Control)
        0xA1, 0x01,       //   Collection (Application)
        0x85, 0x01,       //     Report ID (1)
        0x19, 0xCD,       //     Usage Minimum (0xCD)
        0x29, 0xCF,       //     Usage Maximum (0xCF)
        0x95, 0x03,       //     Report Count (3)
        0x75, 0x01,       //     Report Size (1)
        0x81, 0x02,       //     Input (Data, Variable, Absolute)
        0xC0              //   End Collection
    };

    HidConsumerMap map;
    CHECK(hid_consumer_parse_descriptor(range_descriptor, sizeof(range_descriptor), &map));
    CHECK_EQ(map.usage_count, 3);
    CHECK_EQ(map.usages[0].usage, 0xCD);
    CHECK_EQ(map.usages[2].usage, 0xCF);
}

TEST_CASE("hid_consumer_parse_descriptor rejects non-consumer and mismatched descriptors") {
    HidConsumerMap map;

    // A boot keyboard descriptor: keyboard page, array field -- not a consumer bitmap.
    static const uint8_t keyboard_descriptor[] = {
        0x05, 0x01,       // Usage Page (Generic Desktop)
        0x09, 0x06,       //   Usage (Keyboard)
        0xA1, 0x01,       //   Collection (Application)
        0x95, 0x08,       //     Report Count (8)
        0x75, 0x01,       //     Report Size (1)
        0x81, 0x02,       //     Input (Data, Variable, Absolute) -- modifier byte
        0x95, 0x06,       //     Report Count (6)
        0x75, 0x08,       //     Report Size (8)
        0x15, 0x00,       //     Logical Minimum (0)
        0x25, 0x65,       //     Logical Maximum (101)
        0x05, 0x07,       //     Usage Page (Keyboard)
        0x19, 0x00,       //     Usage Minimum (0)
        0x29, 0x65,       //     Usage Maximum (101)
        0x81, 0x00,       //     Input (Data, Array)
        0xC0              //   End Collection
    };
    CHECK(!hid_consumer_parse_descriptor(keyboard_descriptor, sizeof(keyboard_descriptor), &map));
    CHECK_EQ(map.usage_count, 0);

    // Usage count that doesn't match the report count is not mapped (never guess).
    static const uint8_t mismatch_descriptor[] = {
        0x05, 0x0C,       // Usage Page (Consumer)
        0x09, 0x01,       //   Usage (Consumer Control)
        0xA1, 0x01,       //   Collection (Application)
        0x09, 0xCD,       //     Usage (Play/Pause)
        0x95, 0x04,       //     Report Count (4) -- but only one usage listed
        0x75, 0x01,       //     Report Size (1)
        0x81, 0x02,       //     Input (Data, Variable, Absolute)
        0xC0              //   End Collection
    };
    CHECK(!hid_consumer_parse_descriptor(mismatch_descriptor, sizeof(mismatch_descriptor), &map));

    // Null arguments and empty descriptors are rejected.
    CHECK(!hid_consumer_parse_descriptor(nullptr, 0, &map));
    const uint8_t empty[] = { 0xC0 };
    CHECK(!hid_consumer_parse_descriptor(empty, sizeof(empty), &map));
}
