#pragma once

#include "tt_app.h"
#include "tt_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start an app that displays a list of items and allows the user to select one.
 * @param[in] title the title to show in the toolbar
 * @param[in] argc the amount of items that the list contains
 * @param[in] argv the labels of the items in the list
 * @return the launch ID of the dialog, which can be compared in onResult to identify the source
 */
AppLaunchId tt_app_selectiondialog_start(const char* title, int argc, const char* argv[]);

/** @return the index of the item that was clicked by the user, or -1 when the user didn't select anything */
int32_t tt_app_selectiondialog_get_result_index(BundleHandle handle);

#ifdef __cplusplus
}
#endif