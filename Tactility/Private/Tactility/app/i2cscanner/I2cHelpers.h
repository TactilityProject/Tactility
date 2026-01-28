#pragma once

#include <string>
#include <cstdint>

namespace tt::app::i2cscanner {

std::string getAddressText(uint8_t address);

std::string getPortNamesForDropdown();

bool getFirstActiveI2cPort(int32_t& out);

bool getActivePortAtIndex(int32_t index, int32_t& out);

}
