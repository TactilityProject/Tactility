#pragma once

#include <Tactility/hal/sdcard/SdCardDevice.h>
#include <memory>

using tt::hal::sdcard::SdCardDevice;

std::shared_ptr<SdCardDevice> createSdCard();
