#pragma once

#include <cstdint>
#include <string>

namespace tt::app::fileselection {

/**
 * Show a file selection dialog that allows the user to select an existing file, as a modal
 * child of @a callerAppInstanceId (see app_manager_start_for_result()). Result (0 = Ok,
 * 1 = Cancelled) is delivered back via APP_EVENT_RESULT once this app's thread exits - call
 * getLastPath() right after receiving it, on result == 0. The caller must call
 * app_manager_stop() on the returned instance id once that event arrives, to fully reap this
 * instance.
 * @return the new app instance id
 */
uint32_t startForExistingFile(uint32_t callerAppInstanceId);

/**
 * Same as startForExistingFile(), but also allows picking a path that doesn't exist yet (for
 * "save as"-style flows).
 */
uint32_t startForExistingOrNewFile(uint32_t callerAppInstanceId);

/** @return the path picked by the last FileSelection dialog that closed with result == Ok. Only
 * one dialog is expected to be open at a time. */
std::string getLastPath();

} // namespace
