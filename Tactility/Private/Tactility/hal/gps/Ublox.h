#pragma once

#include "Tactility/hal/gps/GpsDevice.h"

#include <cstdint>
#include <cstddef>

struct Device;

namespace tt::hal::gps::ublox {

void checksum(uint8_t* message, size_t length);

// From https://github.com/meshtastic/firmware/blob/7648391f91f2b84e367ae2b38220b30936fb45b1/src/gps/GPS.cpp#L128
uint8_t makePacket(uint8_t classId, uint8_t messageId, const uint8_t* payload, uint8_t payloadSize, uint8_t* bufferOut);

GpsModel probe(::Device* uart);

bool init(::Device* uart, GpsModel model);

}
