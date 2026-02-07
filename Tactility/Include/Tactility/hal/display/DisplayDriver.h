#pragma once

#include <Tactility/Lock.h>
#include <cstdint>

namespace tt::hal::display {

enum class ColorFormat {
    Monochrome, // 1 bpp
    BGR565,
    BGR565Swapped,
    RGB565,
    RGB565Swapped,
    RGB888
};

class DisplayDriver {

public:

    virtual ~DisplayDriver() = default;

    virtual ColorFormat getColorFormat() const = 0;
    virtual uint16_t getPixelWidth() const = 0;
    virtual uint16_t getPixelHeight() const = 0;
    virtual bool drawBitmap(int xStart, int yStart, int xEnd, int yEnd, const void* pixelData) = 0;
};

}