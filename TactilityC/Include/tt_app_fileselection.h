#pragma once

#include "tt_app.h"
#include "tt_bundle.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show a file selection dialog that allows the user to select an existing file.
 * @return the launch ID of the dialog, which can be compared in onResult to identify the source
 */
AppLaunchId tt_app_fileselection_start_for_existing_file();

/**
 * Show a file selection dialog that allows the user to select a new or existing file.
 * @return the launch ID of the dialog, which can be compared in onResult to identify the source
 */
AppLaunchId tt_app_fileselection_start_for_existing_or_new_file();

/**
 * @param[in] handle the result bundle passed to onResult
 * @param[out] buffer the buffer to store the selected path in
 * @param[in] bufferSize the size of the buffer (must include room for the null terminator)
 * @return true if a path was selected and written to buffer, false otherwise
 */
bool tt_app_fileselection_get_result_path(BundleHandle handle, char* buffer, uint32_t bufferSize);

#ifdef __cplusplus
}
#endif
