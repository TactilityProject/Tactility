// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** HID Consumer Control usage page (HID Usage Tables, section 15). */
#define HID_USAGE_PAGE_CONSUMER 0x0C

/**
 * Consumer-page usages this system acts on. Standard HID Usage Table values;
 * only usages demonstrated on real hardware are listed.
 */
#define HID_CONSUMER_USAGE_PLAY_PAUSE       0xCD
#define HID_CONSUMER_USAGE_VOLUME_INCREMENT 0xE9
#define HID_CONSUMER_USAGE_VOLUME_DECREMENT 0xEA

/** Maximum number of consumer usages tracked in a HidConsumerMap. */
#define HID_CONSUMER_MAX_USAGES 32

typedef struct {
    uint16_t usage;    /**< Consumer-page usage code (e.g. HID_CONSUMER_USAGE_PLAY_PAUSE). */
    uint8_t bit_index; /**< Bit position within the report payload (after the report ID byte, if any). */
} HidConsumerUsageBit;

/**
 * Parsed mapping from a HID report descriptor's Consumer Control buttons to
 * report bits, for the common "one usage per 1-bit Data/Variable/Absolute
 * input field" layout shared by headsets and media keyboards. Produced by
 * hid_consumer_parse_descriptor().
 */
typedef struct {
    bool has_report_id;    /**< Whether the mapped field lives behind a Report ID byte. */
    uint8_t report_id;     /**< Report ID preceding the bitmap, when has_report_id. */
    size_t usage_count;    /**< Number of valid entries in usages. */
    HidConsumerUsageBit usages[HID_CONSUMER_MAX_USAGES];
    size_t payload_bytes;  /**< Bitmap bytes following the (optional) report ID byte. */
} HidConsumerMap;

/**
 * Parses a HID report descriptor looking for a Consumer-page button bitmap.
 *
 * Only 1-bit Data/Variable/Absolute input fields whose usages all belong to
 * the Consumer page are mapped: the shape used by standard Consumer Control
 * collections (headset buttons, media keyboards). Other layouts are ignored
 * rather than guessed at, and the first matching field wins, so a descriptor
 * that also exposes consumer bits in some exotic second field still decodes
 * its primary field.
 *
 * @param descriptor raw report descriptor bytes
 * @param length descriptor length in bytes
 * @param out_map receives the mapping; only valid when returning true
 * @return true when at least one consumer usage was mapped, false otherwise
 */
bool hid_consumer_parse_descriptor(const uint8_t* descriptor, size_t length, HidConsumerMap* out_map);

/**
 * Decodes an input report against a map produced by hid_consumer_parse_descriptor().
 *
 * @param map mapping from hid_consumer_parse_descriptor()
 * @param report raw input report bytes (including the report ID byte when the
 *               descriptor declares one)
 * @param length report length in bytes
 * @param out_usages receives the consumer usages whose bits are currently set
 * @param max_usages capacity of out_usages
 * @return the number of usages written to out_usages; 0 when the report does
 *         not match the map's report ID, is too short, or arguments are null
 */
size_t hid_consumer_decode_report(const HidConsumerMap* map, const uint8_t* report, size_t length,
                                  uint16_t* out_usages, size_t max_usages);

#ifdef __cplusplus
}
#endif
