// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Tactility/hal/touch/TouchDriver.h>

#include <tactility/device.h>

namespace tt::hal::touch {

/** Wraps a TactilityKernel Device of type POINTER_TYPE as a TouchDriver. */
class KernelTouchDriver final : public TouchDriver {

    ::Device* device;

public:

    explicit KernelTouchDriver(::Device* device);

    bool getTouchedPoints(uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* pointCount, uint8_t maxPointCount) override;
};

}
