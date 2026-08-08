#pragma once

#include <app/manager.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * Show a dialog with the provided title, message and 0, 1 or more buttons.
 * @warning AlertDialog is now a new-model app (see Modules/app-module); it delivers its result
 * via APP_EVENT_RESULT to a caller's app_instance_id, which side-loaded ELF apps don't have.
 * The dialog will show, but this app's onResult callback will NOT be invoked with the button
 * that was pressed - there is currently no bridge back to the old ELF app result mechanism.
 * @param[in] parent_id parent app ID or 0
 * @param[in] title the title to show in the toolbar
 * @param[in] message the message to display
 * @param[in] buttonLabels the buttons to show, or null when there are none to show
 * @param[in] buttonLabelCount the amount of buttons (0 or more)
 * @return the launch ID of the dialog (kept for source compatibility; no onResult will follow)
 */
AppInstanceId tt_app_alertdialog_start(AppInstanceId parent_id, const char* title, const char* message, const char* buttonLabels[], uint32_t buttonLabelCount);

#ifdef __cplusplus
}
#endif