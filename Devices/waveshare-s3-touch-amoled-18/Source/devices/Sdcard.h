#pragma once

#include <Tactility/hal/sdcard/SpiSdCardDevice.h>

std::shared_ptr<tt::hal::sdcard::SpiSdCardDevice> createSdCard();