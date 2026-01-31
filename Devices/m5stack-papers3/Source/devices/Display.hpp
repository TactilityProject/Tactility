#pragma once

#include <memory>
#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>

// Display
#define PAPERS3_EPD_HORIZONTAL_RESOLUTION 540
#define PAPERS3_EPD_VERTICAL_RESOLUTION 960

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay();
