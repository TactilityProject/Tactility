#pragma once

#include <Tactility/hal/touch/TouchDevice.h>
#include <memory>

std::shared_ptr<tt::hal::touch::TouchDevice> createTouch();
