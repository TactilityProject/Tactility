#pragma once

#include <memory>
#include <Tactility/hal/power/PowerDevice.h>

std::shared_ptr<tt::hal::power::PowerDevice> createPower();
