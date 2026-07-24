// SPDX-License-Identifier: Apache-2.0
#pragma once

// Internal-only result of waiting for a chip's ACK/NACK response during probing/initialization.
// Not part of the public API (see tactility/drivers/gps.h) - callers only ever see GpsState/GpsModel.
enum class GpsResponse {
    None,
    NotAck,
    FrameErrors,
    Ok,
};
