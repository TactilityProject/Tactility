#include "Tactility/app/i2cscanner/I2cHelpers.h"

#include <Tactility/Tactility.h>
#include <Tactility/StringUtils.h>

#include <iomanip>
#include <vector>
#include <sstream>

namespace tt::app::i2cscanner {

std::string getAddressText(uint8_t address) {
    std::stringstream stream;
    stream << "0x"
           << std::uppercase << std::setfill ('0')
           << std::setw(2) << std::hex << (uint32_t)address;
    return stream.str();
}

std::string getPortNamesForDropdown() {
    std::vector<std::string> config_names;
    for (int port = 0; port < I2C_NUM_MAX; ++port) {
        auto native_port = static_cast<i2c_port_t>(port);
        if (hal::i2c::isStarted(native_port)) {
            auto* name = hal::i2c::getName(native_port);
            if (name != nullptr) {
                config_names.push_back(name);
            }
        }
    }
    return string::join(config_names, "\n");
}

bool getActivePortAtIndex(int32_t index, int32_t& out) {
    int current_index = -1;
    for (int port = 0; port < I2C_NUM_MAX; ++port) {
        auto native_port = static_cast<i2c_port_t>(port);
        if (hal::i2c::isStarted(native_port)) {
            current_index++;
            if (current_index == index) {
                out = port;
                return true;
            }
        }
    }
    return false;
}

}
