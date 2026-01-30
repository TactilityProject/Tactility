#pragma once

#include "tt_app.h"
#include "tt_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Show a dialog with the provided title, message and 0, 1 or more buttons.
 * @param[in] title the title to show in the toolbar
 * @param[in] message the message to display
 * @param[in] buttonLabels the buttons to show, or null when there are none to show
 * @param[in] buttonLabelCount the amount of buttons (0 or more)
 * @return the launch ID of the dialog, which can be compared in onResult to identify the source
 */
AppLaunchId tt_app_alertdialog_start(const char* title, const char* message, const char* buttonLabels[], uint32_t buttonLabelCount);

/**
 * @return the index of the button that was clicked (the index in the array when start() was called)
 */
int32_t tt_app_alertdialog_get_result_index(BundleHandle handle);

#ifdef __cplusplus
}
#endif