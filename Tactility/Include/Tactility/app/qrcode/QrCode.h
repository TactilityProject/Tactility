#pragma once

#include "app/instance.h"

#include <string>

/**
 * An app with a toolbar and a QR view that fills the rest of the app area.
 */
namespace tt::app::qrcode {

/**
 * Show a QR code of @a text.
 * @return the new app's instance id, or 0 when it couldn't be started
 */
AppInstanceId start(const std::string& text);

}
