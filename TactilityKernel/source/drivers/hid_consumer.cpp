// SPDX-License-Identifier: Apache-2.0

#include <tactility/drivers/hid_consumer.h>

#include <string.h>

namespace {

// Upper bound on usages accumulated for one input field before the field's
// Input main item arrives. Generous compared to real Consumer Control
// descriptors (which list a handful), but keeps a malicious/garbage descriptor
// from looping or overflowing fixed storage: anything past this marks the
// field unmappable.
constexpr size_t PENDING_USAGE_CAP = 64;

struct PendingUsages {
    uint16_t items[PENDING_USAGE_CAP];
    size_t count = 0;
    bool range_open = false; // a Usage Minimum was seen without its Usage Maximum yet
    uint16_t range_min = 0;
    bool overflow = false;

    void reset() {
        count = 0;
        range_open = false;
        overflow = false;
    }

    void add(uint16_t usage) {
        if (count < PENDING_USAGE_CAP) {
            items[count++] = usage;
        } else {
            overflow = true;
        }
    }
};

// HID item prefix layout: bits 0-1 size, bits 2-3 type, bits 4-7 tag. Tags are scoped by
// item type, e.g. tag 0x00 is Usage Page in a Global item but Usage in a Local item.
constexpr uint8_t ITEM_TYPE_MAIN = 0x00;
constexpr uint8_t ITEM_TYPE_GLOBAL = 0x01;
constexpr uint8_t ITEM_TYPE_LOCAL = 0x02;

constexpr uint8_t ITEM_TAG_INPUT = 0x80;
constexpr uint8_t ITEM_TAG_OUTPUT = 0x90;
constexpr uint8_t ITEM_TAG_FEATURE = 0xB0;
constexpr uint8_t ITEM_TAG_COLLECTION = 0xA0;
constexpr uint8_t ITEM_TAG_END_COLLECTION = 0xC0;

constexpr uint8_t ITEM_TAG_USAGE_PAGE = 0x00;
constexpr uint8_t ITEM_TAG_LOGICAL_MIN = 0x10;
constexpr uint8_t ITEM_TAG_LOGICAL_MAX = 0x20;
constexpr uint8_t ITEM_TAG_REPORT_ID = 0x80;
constexpr uint8_t ITEM_TAG_REPORT_SIZE = 0x70;
constexpr uint8_t ITEM_TAG_REPORT_COUNT = 0x90;

constexpr uint8_t ITEM_TAG_USAGE = 0x00;
constexpr uint8_t ITEM_TAG_USAGE_MIN = 0x10;
constexpr uint8_t ITEM_TAG_USAGE_MAX = 0x20;

uint32_t read_item_value(const uint8_t* data, uint8_t size) {
    // HID items are little-endian; size is 0, 1, 2 or 4 bytes.
    switch (size) {
        case 1: return data[0];
        case 2: return (uint32_t) data[0] | ((uint32_t) data[1] << 8);
        case 4: return (uint32_t) data[0]
            | ((uint32_t) data[1] << 8)
            | ((uint32_t) data[2] << 16)
            | ((uint32_t) data[3] << 24);
        default: return 0;
    }
}

} // namespace

extern "C" {

bool hid_consumer_parse_descriptor(const uint8_t* descriptor, size_t length, HidConsumerMap* out_map) {
    if (descriptor == nullptr || out_map == nullptr) {
        return false;
    }
    memset(out_map, 0, sizeof(*out_map));

    uint32_t usage_page = 0;
    uint8_t report_size = 0;
    uint8_t report_count = 0;
    bool has_report_id = false;
    uint8_t report_id = 0;
    PendingUsages pending = {};

    size_t i = 0;
    while (i < length) {
        uint8_t prefix = descriptor[i++];
        if (prefix == 0xFE) {
            // Long item: prefix, size byte, tag/type byte, data. Not used by
            // Consumer Control descriptors; skip wholeheartedly.
            if (i >= length) {
                break;
            }
            uint8_t long_size = descriptor[i++];
            i += 1 + long_size;
            continue;
        }

        uint8_t raw_size = prefix & 0x03;
        uint8_t size = (raw_size == 3) ? 4 : raw_size; // bSize==3 encodes a 4-byte item
        uint8_t type = (prefix >> 2) & 0x03;
        uint8_t tag = prefix & 0xF0;
        if (size > 0 && i + size > length) {
            break; // truncated descriptor
        }
        uint32_t value = read_item_value(descriptor + i, size);
        i += size;

        if (type == ITEM_TYPE_GLOBAL) {
            switch (tag) {
                case ITEM_TAG_USAGE_PAGE:
                    usage_page = value;
                    break;
                case ITEM_TAG_REPORT_ID:
                    has_report_id = true;
                    report_id = (uint8_t) value;
                    break;
                case ITEM_TAG_REPORT_SIZE:
                    report_size = (uint8_t) value;
                    break;
                case ITEM_TAG_REPORT_COUNT:
                    report_count = (uint8_t) value;
                    break;
                default:
                    break; // Logical Min/Max etc. don't affect button-bit mapping
            }
            continue;
        }

        if (type == ITEM_TYPE_LOCAL) {
            switch (tag) {
                case ITEM_TAG_USAGE:
                    pending.add((uint16_t) value);
                    break;
                case ITEM_TAG_USAGE_MIN:
                    pending.range_open = true;
                    pending.range_min = (uint16_t) value;
                    break;
                case ITEM_TAG_USAGE_MAX: {
                    if (pending.range_open) {
                        pending.range_open = false;
                        uint16_t range_max = (uint16_t) value;
                        if (range_max >= pending.range_min) {
                            for (uint32_t usage = pending.range_min; usage <= range_max; usage++) {
                                pending.add((uint16_t) usage);
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }
            continue;
        }

        if (type == ITEM_TYPE_MAIN) {
            if (tag == ITEM_TAG_INPUT) {
                // Data(0)/Constant(1), Array(0)/Variable(2) flags live in the low bits of the
                // Input item's value: Data/Variable/Absolute == 0x02.
                bool is_button_bitmap_field =
                    (value & 0x03) == 0x02
                    && report_size == 1
                    && report_count > 0
                    && report_count <= HID_CONSUMER_MAX_USAGES
                    && pending.count == report_count
                    && !pending.overflow
                    && usage_page == HID_USAGE_PAGE_CONSUMER;

                if (is_button_bitmap_field && out_map->usage_count == 0) {
                    out_map->has_report_id = has_report_id;
                    out_map->report_id = report_id;
                    out_map->usage_count = report_count;
                    out_map->payload_bytes = (report_count + 7) / 8;
                    for (size_t u = 0; u < report_count; u++) {
                        out_map->usages[u].usage = pending.items[u];
                        out_map->usages[u].bit_index = (uint8_t) u;
                    }
                }
                // The pending usages belong to this field whether or not it was mapped.
                pending.reset();
            } else {
                // Output/Feature fields and Collections consume pending usages: in
                // particular, the usage(s) declared before an Application collection (e.g.
                // "Usage (Consumer Control)") describe the collection, not its buttons.
                pending.reset();
            }
            continue;
        }
    }

    return out_map->usage_count > 0;
}

size_t hid_consumer_decode_report(const HidConsumerMap* map, const uint8_t* report, size_t length,
                                  uint16_t* out_usages, size_t max_usages) {
    if (map == nullptr || report == nullptr || out_usages == nullptr || map->usage_count == 0) {
        return 0;
    }

    const uint8_t* payload = report;
    size_t payload_length = length;
    if (map->has_report_id) {
        if (length < 1 || report[0] != map->report_id) {
            return 0;
        }
        payload = report + 1;
        payload_length = length - 1;
    }

    if (payload_length < map->payload_bytes) {
        return 0;
    }

    size_t found = 0;
    for (size_t u = 0; u < map->usage_count && found < max_usages; u++) {
        uint8_t bit_index = map->usages[u].bit_index;
        if ((payload[bit_index / 8] >> (bit_index % 8)) & 0x01) {
            out_usages[found++] = map->usages[u].usage;
        }
    }
    return found;
}

} // extern "C"
