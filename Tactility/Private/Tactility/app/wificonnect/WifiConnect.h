#pragma once

#include <string>

namespace tt::app::wificonnect {

/**
 * Start the app with optional pre-filled fields.
 */
void start(const std::string& ssid = "", const std::string& password = "");

} // namespace
