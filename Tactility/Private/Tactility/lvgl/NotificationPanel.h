#pragma once

#include <lvgl.h>

#include <cstddef>

namespace tt::lvgl {

/** The list of notifications of notification-module, with a button to clear them. It keeps itself up to date while it exists. */
struct NotificationPanel;

/** Called when the number of notifications that can be cleared (the ones that aren't sticky) changed, on the LVGL thread */
using NotificationCountCallback = void (*)(void* userData, size_t count);

/**
 * Creates the panel under @a parent and subscribes to the notifications.
 * The panel can be moved to another parent with lv_obj_set_parent().
 * @param[in] onCount called now and whenever the number of clearable notifications changes, can be NULL
 * @return NULL when it couldn't be created
 * @warning Caller must hold the LVGL lock.
 */
NotificationPanel* notificationPanelCreate(lv_obj_t* parent, NotificationCountCallback onCount, void* userData);

/** The widget that holds the panel, which is deleted with its parent */
lv_obj_t* notificationPanelGetRoot(const NotificationPanel* panel);

/**
 * Unsubscribes and stops updating. Call it before the widgets of the panel are deleted.
 * @warning Caller must hold the LVGL lock.
 */
void notificationPanelDestroy(NotificationPanel* panel);

}
