#pragma once

#include <string>

namespace tt::app::imageviewer {

/**
 * Show a full-screen viewer for a single image file. Fire-and-forget: doesn't report any result
 * back to the caller.
 * @param file the path to the image file to display
 */
void start(const std::string& file);

}