#pragma once

#include <string>

namespace tt::app::wificonnect {

/**
 * Start the app with optional pre-filled fields.
 * @param[in] hideStatusbar hides the statusbar while the app is shown (the "--hide-statusbar" argument)
 */
void start(const std::string& ssid = "", const std::string& password = "", bool hideStatusbar = false);

} // namespace
