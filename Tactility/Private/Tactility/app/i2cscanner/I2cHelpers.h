#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct Device;

namespace tt::app::i2cscanner {

std::string getAddressText(uint8_t address);

/** The names of the ready I2C controllers, in the order of getActivePortAtIndex() */
std::vector<std::string> getPortNames();

bool getActivePortAtIndex(int32_t index, struct Device** out);

}
