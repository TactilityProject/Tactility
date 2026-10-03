// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "instance.h"

#include <stdbool.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Sends a POSIX signal to an app instance. A signal's pid is its AppInstanceId.
 *
 * An app with an event subscription (app/event.h) receives the signal as an AppEvent: SIGTERM as
 * APP_EVENT_CLOSE, any other signal as APP_EVENT_SIGNAL. Its signal() handlers are not used.
 *
 * Any other app gets the signal queued, and delivered on its own task at its next libc call (see
 * app_signal_deliver_pending()). A blocking read, write, poll or sleep it is waiting in returns -1
 * with errno EINTR within about 100 ms.
 * A signal the app ignores is discarded right away.
 *
 * @param[in] sig 1 to APP_LIBC_SIGNAL_COUNT - 1 (app/libc.h)
 * @retval ERROR_NONE the signal was sent or discarded
 * @retval ERROR_INVALID_ARGUMENT @a sig is out of range
 * @retval ERROR_NOT_FOUND no such app instance
 * @retval ERROR_RESOURCE an event queue of the app was full
 */
error_t app_signal_send(AppInstanceId app_instance_id, int sig);

/** @return true when the calling app instance has a signal that was not yet delivered */
bool app_signal_is_pending(void);

/**
 * Delivers the calling app instance's pending signals: calls signal() handlers, and ends the app
 * (as exit(128 + sig)) for a signal without a handler whose default action terminates.
 * Does nothing outside an app task.
 * @warning Only call where the app holds no lock of the system itself, e.g. at the entry of a libc wrap.
 */
void app_signal_deliver_pending(void);

#ifdef __cplusplus
}
#endif
