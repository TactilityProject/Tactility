// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identifies how a download finished, as delivered to http_download_poll(). */
enum HttpDownloadEventType {
    HTTP_DOWNLOAD_EVENT_SUCCESS, // no data
    HTTP_DOWNLOAD_EVENT_ERROR, // struct HttpDownloadErrorEvent
    HTTP_DOWNLOAD_EVENT_CANCELLED, // no data - see http_download_cancel()
};

#define HTTP_DOWNLOAD_ERROR_MESSAGE_MAX_LEN 64

/** Data for HTTP_DOWNLOAD_EVENT_ERROR. */
struct HttpDownloadErrorEvent {
    char message[HTTP_DOWNLOAD_ERROR_MESSAGE_MAX_LEN];
};

/** Event delivered through http_download_poll() once a download's task finishes. */
struct HttpDownloadEvent {
    enum HttpDownloadEventType type;
    /** Microseconds since boot, from get_micros_since_boot(). */
    uint64_t timestamp;
    /** The server's HTTP response status code, or 0 if no response was ever received
     * (e.g. failed to open the connection, or platform doesn't support downloads). */
    int32_t status_code;
    /** Valid only when type == HTTP_DOWNLOAD_EVENT_ERROR. */
    struct HttpDownloadErrorEvent error;
};

/** Internal, refcounted state shared between a subscription and its download task - lets
 * http_download_unsubscribe() sever the link safely at any time, even mid-download (see its
 * own doc). Defined only in download.cpp; opaque here like Module::internal (tactility/module.h). */
struct HttpDownloadLink;

/**
 * Caller-owned subscription node for one download's outcome, registered with
 * http_download_subscribe() and polled with http_download_poll() - same subscribe/await/poll
 * shape as TactilityKernel's system_event and app-module's app_event. Unlike those (which
 * multiplex many emitters/subscribers by type or app instance), one subscription always belongs
 * to exactly one download and receives exactly one terminal event.
 * @warning Fields other than `bit` are for internal use only; do not read or write them directly.
 */
struct HttpDownloadSubscription {
    /** Set by http_download_subscribe(). Read-only for the caller: OR it into a
     * task_event_group_wait() mask (alongside other subscriptions sharing the same
     * `internal.event_group`) to block on this subscription and other event sources with one
     * call. */
    uint32_t bit;

    struct {
        /** Caller-owned, borrowed; set by http_download_subscribe(). */
        struct TaskEventGroup* event_group;
        struct HttpDownloadEvent event;
        bool pending;
        struct HttpDownloadLink* link;
    } internal;
};

/**
 * Register a poll subscription for one download's outcome.
 * @warning Does not work in ISR context.
 * @warning Must be called before http_download_start(), so the download task can never finish
 * before a subscriber exists to notify.
 * @param[in,out] sub subscription to register; caller owns the storage and must keep it alive
 * (and stationary) until http_download_unsubscribe()
 * @param[in] event_group caller-owned group to wait on; must outlive @a sub. To block for the
 * outcome, call task_event_group_wait()/task_event_group_wait_any() on this group (OR sub->bit
 * into the mask, or use _wait_any() to include every subscription sharing it), then poll with
 * http_download_poll().
 * @retval ERROR_NONE on success
 * @retval ERROR_RESOURCE @a event_group has no free bits left to claim; @a sub was not registered
 * @retval ERROR_OUT_OF_MEMORY failed to allocate @a sub's internal link; @a sub was not registered
 */
error_t http_download_subscribe(struct HttpDownloadSubscription* sub, struct TaskEventGroup* event_group);

/**
 * Remove a previously registered subscription. Safe to call at any time, including while the
 * download is still in flight (e.g. right after http_download_cancel()) - the download task
 * never touches @a sub or its event_group again once this returns, so both may be destructed
 * immediately afterward without waiting for the download to actually finish.
 * @warning Does not work in ISR context.
 * @return ERROR_NONE on success, ERROR_NOT_FOUND if @a sub isn't currently subscribed
 */
error_t http_download_unsubscribe(struct HttpDownloadSubscription* sub);

/**
 * Non-blocking: check whether the download's terminal event has arrived.
 * @warning Never blocks. To wait, block in task_event_group_wait()/task_event_group_wait_any()
 * on @a sub's bit first (see http_download_subscribe()), then call this.
 * @param[in,out] sub subscription to poll, as passed to http_download_subscribe()
 * @param[out] out_event set to the download's outcome
 * @retval ERROR_NONE the terminal event arrived - it is copied into @a out_event
 * @retval ERROR_TIMEOUT the download hasn't finished yet
 */
error_t http_download_poll(struct HttpDownloadSubscription* sub, struct HttpDownloadEvent* out_event);

/**
 * Starts a download on its own dedicated task: issues a GET request for @a url, verifying the
 * server against the PEM certificate @a cert_pem, and writes the response body to
 * @a target_path.
 * @param[in] url the URL to download
 * @param[in] cert_pem PEM certificate text used to verify the server, copied before this returns.
 * Empty uses the trusted root certificates of the platform: the certificate bundle on ESP32, the system certificates on the simulator.
 * @param[in] target_path path to write the downloaded file to
 * @param[in] sub subscription to notify on completion, already registered via
 * http_download_subscribe(); must stay alive (and stationary) at least until
 * http_download_unsubscribe() is called
 * @retval ERROR_NONE the download task was started
 * @retval ERROR_INVALID_ARGUMENT @a sub is not currently subscribed
 * @retval ERROR_OUT_OF_MEMORY failed to allocate the download task
 */
error_t http_download_start(
    const char* url,
    const char* cert_pem,
    const char* target_path,
    struct HttpDownloadSubscription* sub
);

/**
 * Request cancellation of @a sub's in-flight download. Best-effort and asynchronous: the
 * download task only checks for this periodically (between I/O steps), so it keeps running for
 * a short while after this returns. If you still want the outcome, poll as usual - it finishes
 * with HTTP_DOWNLOAD_EVENT_CANCELLED. If you don't, http_download_unsubscribe() may be called
 * right away instead of waiting for that - see its own doc.
 * @param[in,out] sub subscription for the download to cancel, as passed to
 * http_download_subscribe()
 * @retval ERROR_NONE the cancellation request was recorded
 * @retval ERROR_INVALID_STATE @a sub is not currently subscribed
 */
error_t http_download_cancel(struct HttpDownloadSubscription* sub);

#ifdef __cplusplus
}
#endif
