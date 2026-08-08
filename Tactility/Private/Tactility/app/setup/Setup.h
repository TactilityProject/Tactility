#pragma once

namespace tt::app::setup {

void start();

/** @return true if the setup wizard has already run to completion */
bool isCompleted();

}
