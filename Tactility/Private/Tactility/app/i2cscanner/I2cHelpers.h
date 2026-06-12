#pragma once

#include <string>
#include <cstdint>

struct Device;

namespace tt::app::i2cscanner {

std::string getAddressText(uint8_t address);

std::string getPortNamesForDropdown();

bool getActivePortAtIndex(int32_t index, struct Device** out);

}
