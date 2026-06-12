#include "Tactility/app/i2cscanner/I2cHelpers.h"

#include <Tactility/StringUtils.h>

#include <tactility/drivers/i2c_controller.h>

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
    device_for_each_of_type(&I2C_CONTROLLER_TYPE, &config_names, [](auto* device, auto* context) {
        auto* names = static_cast<std::vector<std::string>*>(context);
        if (device_is_ready(device)) {
            names->push_back(device->name);
        }
        return true;
    });
    return string::join(config_names, "\n");
}

bool getActivePortAtIndex(int32_t index, struct Device** out) {
    struct Context {
        int32_t targetIndex;
        int32_t currentIndex;
        struct Device* foundDevice;
    } context = { index, 0, nullptr };

    device_for_each_of_type(&I2C_CONTROLLER_TYPE, &context, [](auto* device, auto* ctx) {
        auto* c = static_cast<Context*>(ctx);
        if (device_is_ready(device)) {
            if (c->currentIndex == c->targetIndex) {
                c->foundDevice = device;
                return false;
            }
            c->currentIndex++;
        }
        return true;
    });
    *out = context.foundDevice;
    return context.foundDevice != nullptr;
}

}
