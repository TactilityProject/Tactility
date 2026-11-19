// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/bundle.h>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Notifications for apps and services. A notification is built in a NotificationContext, like an app is started
 * with an AppStartContext, and then sent. The most recent notifications are kept until they are dismissed.
 *
 * Steps:
 *  - Create a context with notification_context_for_app() or notification_context_for_service()
 *  - Optionally set a title and severity, and put key/value data in the Bundle of notification_context_get_data()
 *  - Call notification_send()
 */

enum NotificationSourceType {
    NOTIFICATION_SOURCE_APP,
    NOTIFICATION_SOURCE_SERVICE,
};

enum NotificationSeverity {
    NOTIFICATION_SEVERITY_INFO,
    NOTIFICATION_SEVERITY_WARNING,
    NOTIFICATION_SEVERITY_ERROR,
};

#define NOTIFICATION_SOURCE_ID_MAX_LENGTH 32
#define NOTIFICATION_TITLE_MAX_LENGTH 47
#define NOTIFICATION_MESSAGE_MAX_LENGTH 159
/** The number of notifications that are kept. The oldest one is removed when a new one doesn't fit. */
#define NOTIFICATION_HISTORY_CAPACITY 16

/**
 * What happens when the user acts on a notification, for example taps it. See notification_context_set_action().
 *
 * @warning The caller of notification_invoke_action() is usually the UI, which runs on a task with a small call stack
 * and holds locks. The function must do almost nothing itself: forward the request to a task of its own,
 * for example by signalling an event group, queueing it on a dispatcher or starting an app, and return.
 * It must not block, wait for a lock, read files or do anything else that takes time or a lot of stack.
 * @param[in] notification_id the notification that was acted on. Read it with notification_get() from the task if needed.
 * @param[in] user_data the pointer that was passed to notification_context_set_action()
 */
typedef void (*NotificationAction)(uint32_t notification_id, void* user_data);

struct Notification {
    /** Unique and increasing, never 0 */
    uint32_t id;
    enum NotificationSourceType source_type;
    char source_id[NOTIFICATION_SOURCE_ID_MAX_LENGTH + 1];
    /** Empty when there is none */
    char title[NOTIFICATION_TITLE_MAX_LENGTH + 1];
    /** Never empty */
    char message[NOTIFICATION_MESSAGE_MAX_LENGTH + 1];
    enum NotificationSeverity severity;
    /** Seconds since the Unix epoch, 0 when the clock wasn't set yet */
    int64_t wall_time;
    /** Milliseconds since boot */
    uint64_t uptime_ms;
    /**
     * Optional key/value data, NULL when the sender added none.
     * The notification system owns the Bundle that the sender put in the context: it took it over when the context was
     * sent. The reader gets a clone from notification_get() and must free it with notification_release().
     */
    Bundle* data;
    /** NULL when there is none. Call it with notification_invoke_action() and don't call it directly. */
    NotificationAction action;
    void* action_data;
    /** See notification_context_set_sticky() */
    bool sticky;
};

/**
 * notification_get() and notification_get_all() give the reader its own clone of the data, so it can be kept after
 * the notification was dismissed or removed. Call this once for every notification that was returned.
 */
void notification_release(struct Notification* notification);

/**
 * The fields are for internal use only. A context lives on the caller's stack.
 */
struct NotificationContext {
    enum NotificationSourceType source_type;
    char source_id[NOTIFICATION_SOURCE_ID_MAX_LENGTH + 1];
    char title[NOTIFICATION_TITLE_MAX_LENGTH + 1];
    char message[NOTIFICATION_MESSAGE_MAX_LENGTH + 1];
    enum NotificationSeverity severity;
    /** Owned by the context until it is sent, and by the notification system after that, when it is set to NULL */
    Bundle* data;
    NotificationAction action;
    void* action_data;
    bool sticky;
};

/**
 * Text that is longer than its limit is cut at a character boundary of UTF-8.
 * @param[in] app_id the id of the app that sends the notification
 * @param[in] message the text of the notification, which can't be empty
 */
struct NotificationContext notification_context_for_app(const char* app_id, const char* message);

/**
 * @param[in] service_id the id of the service that sends the notification
 * @param[in] message the text of the notification, which can't be empty
 */
struct NotificationContext notification_context_for_service(const char* service_id, const char* message);

void notification_context_set_title(struct NotificationContext* context, const char* title);

void notification_context_set_severity(struct NotificationContext* context, enum NotificationSeverity severity);

/**
 * The optional key/value data of the notification. Fill it with bundle_put_string(), bundle_put_int32() and so on,
 * for example with what is needed to open the right screen later. The module doesn't interpret it, and keys and
 * values have no length limit. The Bundle is allocated by the first call, so a notification without data costs nothing.
 *
 * Ownership: the context owns the Bundle until it is passed to notification_send() or notification_update(). On
 * success the notification system takes it over and the context no longer refers to it, so the caller must not use or
 * free it afterwards, and must not keep a pointer to it. When sending fails, or the context is never sent, the caller
 * still owns it and frees it with notification_context_destroy().
 * @return NULL when the Bundle couldn't be allocated
 */
Bundle* notification_context_get_data(struct NotificationContext* context);

/**
 * Sets the function that runs when the user acts on the notification. A UI that lists notifications calls it
 * through notification_invoke_action(), for example when the notification is tapped.
 *
 * @warning The function runs on the caller's task, which is usually the UI task with a small call stack, so it
 * must only forward the request to a task of its own and return. See NotificationAction.
 * @warning The function must stay valid for as long as the notification can be listed. An app that is unloaded
 * must dismiss its notifications first, because the module can't tell that the function is gone.
 * @param[in] action the function, or NULL for none
 * @param[in] user_data passed to @a action. It must stay valid like the function does.
 */
void notification_context_set_action(struct NotificationContext* context, NotificationAction action, void* user_data);

/**
 * Makes the notification sticky. A UI dismisses a notification that was tapped, after it called the action.
 * A sticky one stays, and the sender decides when it goes by calling notification_dismiss(), for example when what
 * it reports is resolved. The "Clear" button of the UI leaves sticky notifications alone. A notification isn't sticky by default.
 *
 * A notification that isn't sticky is dismissed right after its action returned, so an action that forwards the
 * request to a task can't read the notification from there. Make it sticky and dismiss it from the task when it is done.
 */
void notification_context_set_sticky(struct NotificationContext* context, bool sticky);

/**
 * Sends the notification. On success the ownership of the Bundle of the context is transferred to the notification
 * system, and the context's pointer to it is cleared. Don't use or free the Bundle after that.
 * On failure the context is unchanged, so the caller still owns it.
 * @param[out] out_id the id of the notification, can be NULL
 * The first notification of an app or service adds it to the sources that the user can disable.
 * @param[out] out_id the id of the notification, can be NULL
 * @retval ERROR_INVALID_ARGUMENT when the message or the source id is empty
 * @retval ERROR_INVALID_STATE when the module isn't started
 * @retval ERROR_NOT_ALLOWED when the user disabled the notifications of the sender, in which case the caller still owns the context
 * @retval ERROR_NONE on success
 */
error_t notification_send(struct NotificationContext* context, uint32_t* out_id);

/**
 * Starts a context from a notification that was sent earlier, with its title, message, severity, action, sticky
 * flag and a clone of its data. Change what is needed with the setters of the context and pass it to
 * notification_update(). Free it with notification_context_destroy() when it isn't sent.
 * @retval ERROR_NOT_FOUND when the notification was dismissed or removed
 * @retval ERROR_INVALID_STATE when the module isn't started
 * @retval ERROR_OUT_OF_MEMORY when the data couldn't be cloned
 */
error_t notification_context_for_notification(uint32_t id, struct NotificationContext* context);

/**
 * Replaces the content of a notification that was sent earlier, for example to change a message count.
 * The notification keeps its id. The context is used like in notification_send(): on success the ownership of its
 * Bundle is transferred to the notification system, and the Bundle that the notification had before is freed.
 * @param[in] bump true moves the notification to the newest position and refreshes its timestamps, like a new
 * notification. false keeps its place and timestamps, so a notification that changes often doesn't jump around.
 * @retval ERROR_INVALID_ARGUMENT when the message or the source id is empty
 * @retval ERROR_INVALID_STATE when the module isn't started
 * @retval ERROR_NOT_FOUND when the notification was dismissed or removed, in which case the caller still owns
 * the context and can send it as a new notification
 */
error_t notification_update(uint32_t id, struct NotificationContext* context, bool bump);

/**
 * Frees the Bundle of a context that wasn't sent. Does nothing for a context that was sent, as the notification system
 * owns its Bundle by then.
 */
void notification_context_destroy(struct NotificationContext* context);

/**
 * @param[out] notification receives a copy that must be released with notification_release()
 * @retval ERROR_NOT_FOUND when the notification was dismissed or removed
 */
error_t notification_get(uint32_t id, struct Notification* notification);

/**
 * Reads the kept notifications, newest first. Each must be released with notification_release().
 * @param[out] notifications buffer for the notifications
 * @param[in] max_count capacity of @a notifications
 * @param[out] count the number of notifications returned
 */
error_t notification_get_all(struct Notification* notifications, size_t max_count, size_t* count);

/** @return the number of kept notifications, which is cheaper than reading them */
size_t notification_get_count(void);

/**
 * Dismisses a notification. When an action of it runs on another task, this waits for it to return, so the sender can
 * free what the action uses once this returns. Don't call it from something that such an action waits for.
 * The same goes for notification_dismiss_all(), notification_update() and disabling the sender with
 * notification_set_source_enabled(), which wait for the actions of the notifications that they remove or replace.
 * @retval ERROR_NOT_FOUND when the notification was dismissed or removed
 */
error_t notification_dismiss(uint32_t id);

/**
 * Runs the action of a notification, without holding any lock of the module. The notification stays in the history,
 * so the caller decides whether to dismiss it.
 * @warning The action must only forward the request to a task, see NotificationAction.
 * @retval ERROR_NOT_FOUND when the notification was dismissed or removed
 * @retval ERROR_NOT_SUPPORTED when the notification has no action
 * @retval ERROR_RESOURCE when too many actions run at once, in which case the action wasn't called and the caller can try again later
 * @retval ERROR_NONE when the action was called
 */
error_t notification_invoke_action(uint32_t id);

void notification_dismiss_all(void);

/**
 * Dismisses a notification unless it is sticky. Like notification_dismiss(), it waits for an action of the notification
 * that runs on another task. The flag is read when the notification is removed, under the same
 * lock, so a sender that made it sticky in the meantime is respected.
 * @retval ERROR_NOT_FOUND when the notification was dismissed or removed
 * @retval ERROR_NOT_ALLOWED when the notification is sticky, which stays
 */
error_t notification_dismiss_unless_sticky(uint32_t id);

/** Dismisses all notifications that aren't sticky, without copying any of them. Emits NOTIFICATION_EVENT_DISMISSED for each. */
void notification_dismiss_non_sticky(void);

#define NOTIFICATION_SOURCE_LIMIT 64

/** An app or service that sent a notification at least once */
struct NotificationSource {
    enum NotificationSourceType type;
    char id[NOTIFICATION_SOURCE_ID_MAX_LENGTH + 1];
    /** Whether its notifications are accepted. A new source is enabled. */
    bool enabled;
};

/**
 * Reads the stored sources, which otherwise happens when the first sender or reader needs them.
 * Call it once the file systems are mounted, so no sender has to read the file on its own task.
 * It does nothing when they were read already, or when the module isn't started.
 * @warning Don't call it on the LVGL thread.
 */
void notification_preload(void);

/**
 * Get the apps and services that sent a notification at least once, in the order that they were first seen.
 * The list is kept across restarts. It is read from storage the first time, so don't call it on the LVGL thread.
 * @param[out] sources buffer for the sources
 * @param[in] max_count capacity of @a sources
 * @param[out] count the number of sources returned
 */
error_t notification_get_sources(struct NotificationSource* sources, size_t max_count, size_t* count);

/**
 * Enables or disables the notifications of a sender. Disabling removes its pending notifications, which emits
 * NOTIFICATION_EVENT_DISMISSED for each of them. The choice is kept across restarts.
 * @retval ERROR_NOT_FOUND when the sender never sent a notification
 */
error_t notification_set_source_enabled(enum NotificationSourceType type, const char* id, bool enabled);

enum NotificationEventType {
    /** A notification was sent, see NotificationEvent.id */
    NOTIFICATION_EVENT_POSTED,
    /** A notification was dismissed, or removed because the history was full, see NotificationEvent.id */
    NOTIFICATION_EVENT_DISMISSED,
    /** All notifications were dismissed */
    NOTIFICATION_EVENT_CLEARED,
    /** The content of a notification was replaced, see NotificationEvent.id */
    NOTIFICATION_EVENT_UPDATED,
};

struct NotificationEvent {
    /** Milliseconds since boot */
    uint64_t timestamp;
    enum NotificationEventType type;
    /** 0 for NOTIFICATION_EVENT_CLEARED */
    uint32_t id;
    /** For NOTIFICATION_EVENT_UPDATED: the update moved the notification to the newest position, like a new one */
    bool bumped;
    /** Increases with every event, so it tells what happened before what. Compare it with notification_get_event_order(). */
    uint32_t order;
};

/**
 * @return the order of the latest event. An event with a higher order happened after this call, and one with an equal or
 * lower order before it, which is exact where timestamps can be equal.
 */
uint32_t notification_get_event_order(void);

#define NOTIFICATION_EVENT_QUEUE_CAPACITY 8

/**
 * Caller-owned subscription, used like the mesh and radio event subscriptions: wait on the event group, then drain
 * with notification_poll(). The events carry the id of a notification, and its content is read with
 * notification_get().
 * @warning Fields other than `bit` are for internal use only.
 */
struct NotificationSubscription {
    uint32_t bit;

    struct {
        struct TaskEventGroup* event_group;
        struct NotificationEvent queue[NOTIFICATION_EVENT_QUEUE_CAPACITY];
        uint8_t head;
        uint8_t count;
        struct NotificationSubscription* next;
    } internal;
};

/**
 * A full queue drops the newest events. A subscriber that suspects it missed some can read notification_get_all().
 * @param[in,out] sub subscription to register, must stay alive until unsubscribed
 * @param[in] event_group caller-owned group to wait on, must outlive @a sub
 * @retval ERROR_RESOURCE when @a event_group has no free bits left
 * @retval ERROR_INVALID_STATE when @a sub is already registered, or the module isn't started
 */
error_t notification_subscribe(struct NotificationSubscription* sub, struct TaskEventGroup* event_group);

error_t notification_unsubscribe(struct NotificationSubscription* sub);

/**
 * Non-blocking: pop the next event.
 * @retval ERROR_TIMEOUT when nothing is queued
 */
error_t notification_poll(struct NotificationSubscription* sub, struct NotificationEvent* event);

#ifdef __cplusplus
}
#endif
