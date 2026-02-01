#pragma once

namespace tt::hal::touch {

class TouchDriver {

public:

    /**
     * Get the coordinates for the currently touched points on the screen.
     *
     * @param[in] x array of X coordinates
     * @param[in] y array of Y coordinates
     * @param[in] strength optional array of strengths (nullable)
     * @param[in] pointCount the number of points currently touched on the screen
     * @param[in] maxPointCount the maximum number of points that can be touched at once
     *
     * @return true when touched and coordinates are available
     */
    virtual bool getTouchedPoints(uint16_t* x, uint16_t* y, uint16_t* strength, uint8_t* pointCount, uint8_t maxPointCount) = 0;
};

}
