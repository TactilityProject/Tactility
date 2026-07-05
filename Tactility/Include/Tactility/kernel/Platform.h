#pragma once

namespace tt::kernel {

/** Recognized platform types */
typedef enum {
    PlatformEsp,
    PlatformSimulator
} Platform;

/** @return the platform that Tactility currently is running on. */
Platform getPlatform();

} // namespace
