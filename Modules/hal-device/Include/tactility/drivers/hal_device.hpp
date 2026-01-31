// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "hal_device.h"

#include <memory>
#include <tactility/hal/Device.h>

namespace tt::hal {

/**
 * @brief Get a tt::hal::Device object from a Kernel device.
 * @warning The input device must be of type HAL_DEVICE_TYPE
 * @param kernelDevice The kernel device
 * @return std::shared_ptr<Device>
 */
std::shared_ptr<Device> hal_device_get_device(::Device* kernelDevice);

void hal_device_set_device(::Device* kernelDevice, std::shared_ptr<Device> halDevice);

}
