#pragma once

#include <memory>
#include "Tactility/hal/sdcard/SdCardDevice.h"

using tt::hal::sdcard::SdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard();
