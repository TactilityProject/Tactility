#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <app/stream.h>
#include <tactility/concurrent/task_event_group.h>

namespace tt::app::fileselection {

struct Options {
    /** Allow picking a path that doesn't exist yet (for "save as"-style flows) */
    bool allowNewFile = false;
    /** Allow picking files */
    bool allowFiles = true;
    /** Allow picking a folder: the folder that is currently shown */
    bool allowFolder = false;
    /** Allow picking more than one file */
    bool multiple = false;
    /** Allowed file extensions, with or without leading dot, case-insensitive. Empty allows every file. */
    std::vector<std::string> extensions;
    /** The directory that is shown first. Empty starts at the root of the data (paths_get_root_path()). */
    std::string initialPath;
};

/**
 * Show a file selection dialog that allows the user to select an existing file, as a modal
 * child of @a callerAppInstanceId (see app_start_for_result_with_streams()). Result
 * (0 = Ok, 1 = Cancelled) is delivered back via APP_EVENT_RESULT once this app's thread exits.
 * On result == 0, read the picked path with app_stream_read(&stream, ...) then
 * app_stream_unsubscribe(&stream); on any other result, just app_stream_unsubscribe(&stream).
 * The caller must call app_manager_stop() on the returned instance id once that event arrives,
 * to fully reap this instance.
 * @param[in,out] stream caller-owned storage bound to the started app's stdout; must stay valid
 * until app_stream_unsubscribe() is called on it (see above).
 * @param[in] buffer caller-owned backing storage for @a stream's ring buffer; must stay valid
 * for the same duration as @a stream.
 * @param[in] bufferCapacity size of @a buffer in bytes. The result has to fit in it: the dialog refuses a longer
 * selection and stays open, so the caller isn't blocked. Reserve room for the longest selection that is expected.
 * @param[in] eventGroup the caller's own event group, reused for the stream's readiness bits
 * (see app_stream_subscribe()) - the caller isn't required to actually wait on them itself.
 * @return the new app instance id
 */
uint32_t startForExistingFile(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup);

/**
 * Same as startForExistingFile(), but also allows picking a path that doesn't exist yet (for
 * "save as"-style flows).
 */
uint32_t startForExistingOrNewFile(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup);

/**
 * Same as startForExistingFile(), but with explicit @a options.
 * With options.multiple, the result holds every picked path, each followed by a newline.
 */
uint32_t start(uint32_t callerAppInstanceId, AppStream& stream, void* buffer, size_t bufferCapacity, TaskEventGroup* eventGroup, const Options& options);

} // namespace
