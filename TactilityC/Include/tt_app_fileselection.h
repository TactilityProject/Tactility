#pragma once

#include <app/manager.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Show a file selection dialog that allows the user to select an existing file.
 * @return the launch ID of the dialog
 */
AppInstanceId tt_app_fileselection_start_for_existing_file(AppInstanceId app_id);

/**
 * Show a file selection dialog that allows the user to select a new or existing file.
 * @return the launch ID of the dialog
 */
AppInstanceId tt_app_fileselection_start_for_existing_or_new_file(AppInstanceId app_id);

/**
 * @return the path picked by the last FileSelection dialog that closed with result == Ok (see
 * tt::app::fileselection::getLastPath()). Only one dialog is expected to be open at a time.
 * @param[out] buffer the buffer to store the selected path in
 * @param[in] bufferSize the size of the buffer (must include room for the null terminator)
 * @retval false @a bufferSize was too small - @a buffer is left untouched
 * @retval true @a buffer was filled
 */
bool tt_app_fileselection_get_result_path(char* buffer, uint32_t bufferSize);

#ifdef __cplusplus
}
#endif
