#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>
#include <memory>

std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay(std::shared_ptr<tt::hal::touch::TouchDevice> touch);
