#pragma once

#include <Tactility/hal/display/DisplayDevice.h>
#include <Tactility/hal/touch/TouchDevice.h>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch();
std::shared_ptr<tt::hal::display::DisplayDevice> createDisplay(std::shared_ptr<tt::hal::touch::TouchDevice> touch);
