# Notification module

Lets apps and services post notifications, and lets other code subscribe to them. The module has no UI. A notification
has an optional title, a message, a severity, a source (an app id or a service id), a timestamp and optional key/value data.

- Build a notification with `notification_context_for_app()` or `notification_context_for_service()`, optionally set the
  title and severity, add data to the `Bundle` of `notification_context_get_data()`, and send it with `notification_send()`.
- A notification can have an action, a function that runs when the user taps it. Set it with
  `notification_context_set_action()`. The UI calls it with `notification_invoke_action()` on its own task, which has a small
  call stack, so the function must only forward the request to a task of the sender and return.
- The most recent 16 notifications are kept in memory. Read them with `notification_get()` and `notification_get_all()`
  and remove them with `notification_dismiss()` and `notification_dismiss_all()`. Nothing is persisted.
- Subscribe with `notification_subscribe()`, wait on the event group and drain with `notification_poll()`, like the
  mesh and radio events. Events carry the notification id, the content is read with `notification_get()`.
