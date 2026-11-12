#pragma once

#include <tactility/drivers/trackball.h>

namespace tt::settings::trackball {

bool load(TrackballSettings& settings);

bool save(const TrackballSettings& settings);

}
