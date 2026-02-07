#pragma once

#include <tactility/drivers/spi_controller.h>
#include <tactility/device.h>

#include <Tactility/Lock.h>

namespace tt {

class SpiDeviceLock : public Lock {
    ::Device* device;
public:
    SpiDeviceLock(::Device* device) : device(device) { }

    bool lock(TickType_t timeout) const override {
        return spi_controller_try_lock(device, timeout) == ERROR_NONE;
    }

    void unlock() const override {
        spi_controller_unlock(device);
    }
};

}
