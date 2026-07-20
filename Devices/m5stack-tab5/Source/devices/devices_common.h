#pragma once

#include <tactility/device.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// Constructs, parents, binds and starts a dynamically-built Device. Mirrors
// device_construct_add_start(), but with a device_set_parent() call inserted between construct and add
bool construct_add_start(Device* device, Device* parent, const char* compatible);

// Unpacks a vendor-typed init-cmd array (ili9881c_lcd_init_cmd_t / st7123_lcd_init_cmd_t - same
// field layout) into the flattened [cmd, data_len, delay_ms, data_len bytes...] byte encoding that
// ili9881c-module/st7123-module's init-sequence config field expects (the same encoding a
// devicetree "array" property would produce - see e.g. ili9881c-module's binding yaml). Needed
// because these Configs are built dynamically here rather than from a .dts node.
template<typename InitCmd>
void flatten_init_sequence(const InitCmd* cmds, size_t count, std::vector<uint8_t>& out) {
    for (size_t i = 0; i < count; i++) {
        out.push_back(static_cast<uint8_t>(cmds[i].cmd));
        out.push_back(static_cast<uint8_t>(cmds[i].data_bytes));
        out.push_back(static_cast<uint8_t>(cmds[i].delay_ms));
        const auto* data = static_cast<const uint8_t*>(cmds[i].data);
        for (size_t j = 0; j < cmds[i].data_bytes; j++) {
            out.push_back(data[j]);
        }
    }
}
