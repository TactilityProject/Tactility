#pragma once

#include <app/manager.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start an app that displays a list of items and allows the user to select one.
 * @param[in] parent_id parent app ID or 0
 * @param[in] title the title to show in the toolbar
 * @param[in] argc the amount of items that the list contains
 * @param[in] argv the labels of the items in the list
 * @return the app instance ID of the dialog, which can be compared in onResult to identify the source
 */
AppInstanceId tt_app_selectiondialog_start(AppInstanceId parent_id, const char* title, int argc, const char* argv[]);

#ifdef __cplusplus
}
#endif