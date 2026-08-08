#pragma once

#include <cstdint>
#include <string>

/**
 * Show a dialog with a title, a message and a text field.
 */
namespace tt::app::inputdialog {

/**
 * Show a dialog with the provided title, message and prefilled text, as a modal child of
 * @a callerAppInstanceId (a new-model app - see app/manager.h). The caller receives the result
 * as an APP_EVENT_RESULT in its own event loop: 0 = OK (call getLastText() for the entered
 * text), 1 = Cancelled or dismissed without a press. The caller is responsible for calling
 * app_manager_stop() on the returned instance id once it has handled the result.
 * @return the new dialog's app instance id
 */
uint32_t start(uint32_t callerAppInstanceId, const std::string& title, const std::string& message, const std::string& prefilled = "");

/**
 * @return the text entered the last time any InputDialog instance was closed with OK. Only one
 * dialog is expected to be open at a time - call this right after receiving its
 * APP_EVENT_RESULT with result == 0.
 */
std::string getLastText();

}
