#pragma once

#include <Tactility/app/App.h>

namespace tt::app::setup {

LaunchId start();

/** @return true if the setup wizard has already run to completion */
bool isCompleted();

}
