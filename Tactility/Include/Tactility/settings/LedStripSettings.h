#pragma once

#include <cstdint>
#include <tactility/drivers/led_strip.h>

namespace tt::settings::ledstrip {

enum class Pattern : uint8_t {
    Solid,
    Alternating,
    Gradient,
};

struct LedStripSettings {
    bool enabled = true;
    Pattern pattern = Pattern::Alternating;
    uint8_t brightness = 24;
    uint8_t primaryPreset = 6;
    uint8_t secondaryPreset = 7;
    struct LedRgb primaryCustom { 255, 165, 0 }; //Orange
    struct LedRgb secondaryCustom { 0, 0, 255 }; //Blue
};

bool load(const char* deviceName, LedStripSettings& settings);

LedStripSettings loadOrGetDefault(const char* deviceName);

LedStripSettings getDefault();

bool save(const char* deviceName, const LedStripSettings& settings);

error_t apply(struct Device* device, const LedStripSettings& settings);

}