// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <lvgl_window_manager/dialog.h>

#include <tactility/error.h>

#include <cstdint>
#include <string>
#include <vector>

/**
 * Dialogs over the window of an app, with translated default buttons. See <lvgl_window_manager/dialog.h> for how they behave:
 * the result function runs on the dialog's own task, so an app typically stores the result and signals its own event group.
 * The app must have a window that is topmost, and only one dialog is shown at a time.
 */
namespace tt::app::dialog {

/** Shows a message with an OK button. There is no result to handle. */
error_t alert(uint32_t appInstanceId, const std::string& title, const std::string& message);

/** Shows a message with the given buttons. @a onResult gets the index of the pressed button, or WINDOW_DIALOG_DISMISSED. */
error_t alert(uint32_t appInstanceId, const std::string& title, const std::string& message, const std::vector<std::string>& buttonLabels, WindowAlertDialogResultFn onResult, void* userData);

/** Shows a message with a text field and the buttons OK and Cancel. */
error_t input(uint32_t appInstanceId, const std::string& title, const std::string& message, const std::string& prefilled, WindowInputDialogResultFn onResult, void* userData);

/** Shows a list of items. @a onResult gets the index of the selected item, or WINDOW_DIALOG_DISMISSED. With one item, the result is that item without showing anything. */
error_t selection(uint32_t appInstanceId, const std::string& title, const std::vector<std::string>& items, WindowSelectionDialogResultFn onResult, void* userData);

} // namespace
