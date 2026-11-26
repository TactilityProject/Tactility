// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <app/instance.h>
#include <tactility/error.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The result of a dialog that was closed without pressing one of its buttons: a click outside of the dialog, the Escape key,
 * or the window of the app that was buried.
 */
#define WINDOW_DIALOG_DISMISSED (-1)

/**
 * @param[in] button_index the index of the pressed button, or WINDOW_DIALOG_DISMISSED
 * @param[in] user_data the value that was passed when the dialog was shown
 */
typedef void (*WindowAlertDialogResultFn)(int32_t button_index, void* user_data);

/**
 * @param[in] confirmed true when OK was pressed, or Enter was pressed in the text field
 * @param[in] text the entered text, valid during the call. An empty string when the dialog wasn't confirmed.
 * @param[in] user_data the value that was passed when the dialog was shown
 */
typedef void (*WindowInputDialogResultFn)(bool confirmed, const char* text, void* user_data);

/**
 * @param[in] item_index the index of the selected item, or WINDOW_DIALOG_DISMISSED
 * @param[in] user_data the value that was passed when the dialog was shown
 */
typedef void (*WindowSelectionDialogResultFn)(int32_t item_index, void* user_data);

/*
 * Dialogs are modals over the root container of an app. They have their own focus group while they are shown,
 * so the keyboard and encoder can't reach the widgets of the app, and a pointer can't reach them through the dimmed backdrop.
 * The widgets of the app stay as they are: unlike a window, a dialog doesn't bury the window of the app.
 *
 * Only one dialog is shown at a time. A dialog is closed when a button is pressed, when the backdrop is clicked, when Escape is
 * pressed, or when the window of the app stops being topmost.
 *
 * Every dialog runs on a task of its own. The result function is called on that task, after the dialog is gone and the keyboard
 * focus is back with the app. The LVGL lock isn't held then, so the function may take it, show another dialog, or do slow work.
 * When a window of the app is removed, a result function that hasn't started is suppressed; if it is already running,
 * window_manager_remove() waits for it to return before the app can release its callback context.
 * @warning A result function must not synchronously call window_manager_remove() for a window of its app, because removal
 * waits for the result function to finish. Do not hold locks needed by the result function while removing the window.
 */

/**
 * Shows a dialog with a title, a message and buttons.
 * Doesn't block, so it can be called from any task, including LVGL event callbacks.
 * @param[in] app_instance_id the app that the dialog is shown over. It must have a window that is topmost.
 * @param[in] title the title. It is copied.
 * @param[in] message the message, which scrolls when it doesn't fit. It is copied.
 * @param[in] button_labels the texts of the buttons. They are copied.
 * @param[in] button_count the number of buttons, at least 1
 * @param[in] on_result called when the dialog is closed. May be NULL.
 * @param[in] user_data passed to @a on_result
 * @retval ERROR_NONE the dialog is being shown
 * @retval ERROR_INVALID_ARGUMENT no buttons or a missing text
 * @retval ERROR_INVALID_STATE the app has no topmost window
 * @retval ERROR_RESOURCE_BUSY a dialog is already shown
 * @retval ERROR_OUT_OF_MEMORY there is no memory for the dialog or its task
 */
error_t window_manager_alert_dialog(
    AppInstanceId app_instance_id,
    const char* title,
    const char* message,
    const char* const* button_labels,
    uint32_t button_count,
    WindowAlertDialogResultFn on_result,
    void* user_data
);

/**
 * Shows a dialog with a title, a message, a text field and the buttons OK and Cancel.
 * Pressing Enter in the text field acts as OK. Doesn't block, so it can be called from any task, including LVGL event callbacks.
 * @param[in] app_instance_id the app that the dialog is shown over. It must have a window that is topmost.
 * @param[in] title the title. It is copied.
 * @param[in] message the message. It is copied.
 * @param[in] prefilled the text that the field starts with, or NULL. It is copied.
 * @param[in] ok_label the text of the OK button. It is copied.
 * @param[in] cancel_label the text of the Cancel button. It is copied.
 * @param[in] on_result called when the dialog is closed. May be NULL.
 * @param[in] user_data passed to @a on_result
 * @retval ERROR_NONE the dialog is being shown
 * @retval ERROR_INVALID_ARGUMENT a missing text
 * @retval ERROR_INVALID_STATE the app has no topmost window
 * @retval ERROR_RESOURCE_BUSY a dialog is already shown
 * @retval ERROR_OUT_OF_MEMORY there is no memory for the dialog or its task
 */
error_t window_manager_input_dialog(
    AppInstanceId app_instance_id,
    const char* title,
    const char* message,
    const char* prefilled,
    const char* ok_label,
    const char* cancel_label,
    WindowInputDialogResultFn on_result,
    void* user_data
);

/**
 * Shows a dialog with a title and a list of items to choose from.
 * With one item, nothing is shown and the result is that item. Doesn't block, so it can be called from any task, including LVGL event callbacks.
 * @param[in] app_instance_id the app that the dialog is shown over. It must have a window that is topmost.
 * @param[in] title the title. It is copied.
 * @param[in] items the texts of the items. They are copied.
 * @param[in] item_count the number of items, at least 1
 * @param[in] on_result called when the dialog is closed. May be NULL.
 * @param[in] user_data passed to @a on_result
 * @retval ERROR_NONE the dialog is being shown
 * @retval ERROR_INVALID_ARGUMENT no items or a missing text
 * @retval ERROR_INVALID_STATE the app has no topmost window
 * @retval ERROR_RESOURCE_BUSY a dialog is already shown
 * @retval ERROR_OUT_OF_MEMORY there is no memory for the dialog or its task
 */
error_t window_manager_selection_dialog(
    AppInstanceId app_instance_id,
    const char* title,
    const char* const* items,
    uint32_t item_count,
    WindowSelectionDialogResultFn on_result,
    void* user_data
);

/**
 * Shows a dialog with a title and a list of items to choose from, each with an optional icon in front of its text.
 * Behaves like window_manager_selection_dialog().
 * @param[in] icons a glyph of the shared icon font per item (LVGL_ICON_SHARED_* from <lvgl/icons/shared.h>), or NULL for no icon.
 * The array may be NULL for no icons at all. They are copied.
 */
error_t window_manager_selection_dialog_with_icons(
    AppInstanceId app_instance_id,
    const char* title,
    const char* const* items,
    const char* const* icons,
    uint32_t item_count,
    WindowSelectionDialogResultFn on_result,
    void* user_data
);

/**
 * Closes the dialog that is shown, as if it was dismissed. Does nothing when no dialog is shown.
 * @warning Only uses the LVGL lock, so it can be called from LVGL event callbacks.
 */
void window_manager_dialog_dismiss(void);

#ifdef __cplusplus
}
#endif
