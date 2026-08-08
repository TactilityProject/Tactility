#pragma once

#include <cstdint>
#include <string>
#include <vector>

/**
 * Show a dialog with a title, a message and 0, 1 or more buttons.
 */
namespace tt::app::alertdialog {

/**
 * Show a dialog with the provided title, message and buttons, as a modal child of
 * @a callerAppInstanceId (a new-model app - see app/manager.h). The caller receives the
 * result as an APP_EVENT_RESULT in its own event loop: result is the pressed button's index
 * (>= 0), or a value not matching any button (currently always 1) if the dialog was dismissed
 * without a button press. No result_bundle. The caller is responsible for calling
 * app_manager_stop() on the returned instance id once it has handled the result.
 * @return the new dialog's app instance id
 */
uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message, const std::vector<std::string>& buttonLabels);

/** @copydoc start(uint32_t, const std::string&, const std::string&, const std::vector<std::string>&)
 * Shows a single "OK" button. */
uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message);

}
