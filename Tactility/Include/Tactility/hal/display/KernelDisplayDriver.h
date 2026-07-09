// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <Tactility/hal/display/DisplayDriver.h>

#include <tactility/device.h>

namespace tt::hal::display {

/** Wraps a TactilityKernel Device of type DISPLAY_TYPE as a DisplayDriver. */
class KernelDisplayDriver final : public DisplayDriver {

    ::Device* device;

public:

    explicit KernelDisplayDriver(::Device* device);

    ColorFormat getColorFormat() const override;
    uint16_t getPixelWidth() const override;
    uint16_t getPixelHeight() const override;
    bool drawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) override;
    uint8_t getFrameBuffers(void* outBuffers[2]) const override;
};

}
