// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/drivers/gps.h>

#include <cstdint>
#include <cstddef>

struct Device;

namespace gps_ublox {

void checksum(uint8_t* message, size_t length);

// From https://github.com/meshtastic/firmware/blob/7648391f91f2b84e367ae2b38220b30936fb45b1/src/gps/GPS.cpp#L128
uint8_t make_packet(uint8_t class_id, uint8_t message_id, const uint8_t* payload, uint8_t payload_size, uint8_t* buffer_out);

GpsModel probe(Device* uart);

bool init(Device* uart, GpsModel model);

}
