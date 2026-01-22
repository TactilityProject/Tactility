#pragma once

#include <cstdint>

namespace tt::settings::keyboard {

struct KeyboardSettings {
    bool backlightEnabled = false;
    uint8_t backlightBrightness = 0; // 0-255
    bool backlightTimeoutEnabled = false;
    uint32_t backlightTimeoutMs = 0; // Timeout in milliseconds
};

bool load(KeyboardSettings& settings);

KeyboardSettings loadOrGetDefault();

KeyboardSettings getDefault();

bool save(const KeyboardSettings& settings);

}
