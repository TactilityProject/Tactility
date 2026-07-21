#pragma once

#include <Tactility/app/App.h>
#include <Tactility/Bundle.h>

#include <string>

namespace tt::app::fileselection {

/**
 * Show a file selection dialog that allows the user to select an existing file.
 * This app returns the absolute file path as a result.
 */
LaunchId startForExistingFile();

/**
 * Show a file selection dialog that allows the user to select a new or existing file.
 * This app returns the absolute file path as a result.
 */
LaunchId startForExistingOrNewFile();

/**
 * @param bundle the result bundle of an app
 * @return the path from the bundle, or empty string if none is present
 */
std::string getResultPath(const Bundle& bundle);

} // namespace
