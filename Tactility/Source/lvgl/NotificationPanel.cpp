#include <Tactility/lvgl/NotificationPanel.h>
#include <Tactility/lvgl/NotificationSource.h>

#include <Tactility/i18n/Localisation.h>

#include <localisation/tr.h>

#include <notification/notification.h>

#include <lvgl/fonts.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/list.h>

#include <lvgl_window_manager/window_manager.h>

#include <tactility/Memory.hpp>
#include <tactility/concurrent/task_event_group.h>
#include <tactility/log.h>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>
#include <tactility/time.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace tt::lvgl {

constexpr auto* TAG = "NotificationPanel";
constexpr uint32_t UPDATE_INTERVAL_MS = 250;

struct Row {
    lv_obj_t* timeLabel;
    uint64_t uptimeMs;
    tt::std::string ageText;
};

struct NotificationPanel {
    lv_obj_t* root = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* emptyLabel = nullptr;
    lv_timer_t* timer = nullptr;
    TaskEventGroup eventGroup {};
    NotificationSubscription subscription {};
    bool subscribed = false;
    NotificationCountCallback onCount = nullptr;
    void* userData = nullptr;
    /** The last read failed, so the list shows an older snapshot until a later read succeeds */
    bool retryRead = false;
    tt::std::vector<Row> rows;
    /** The notifications of the last read. Kept here, as the LVGL task's stack is small. */
    tt::std::vector<Notification> notifications;
};

/** Short, so that it fits next to the text in a row. The unit needs no translation. */
static tt::std::string formatAge(uint64_t uptimeMs) {
    const uint64_t nowMs = get_millis();
    const uint64_t seconds = nowMs > uptimeMs ? (nowMs - uptimeMs) / 1000 : 0;
    char text[16];
    if (seconds < 60) {
        return tr("now");
    }
    if (seconds < 3600) {
        snprintf(text, sizeof(text), "%um", static_cast<unsigned>(seconds / 60));
    } else if (seconds < 86400) {
        snprintf(text, sizeof(text), "%uh", static_cast<unsigned>(seconds / 3600));
    } else {
        snprintf(text, sizeof(text), "%ud", static_cast<unsigned>(seconds / 86400));
    }
    return text;
}

/** The bar is the secondary color. The mono theme has no colors, so there it takes the text color of the row, which flips when the row is selected. */
static void updateBarColor(lv_obj_t* row) {
    const lv_color_t color = lvgl_theme_is_mono() ? lv_obj_get_style_text_color(row, LV_PART_MAIN) : lv_theme_get_color_secondary(row);
    // The text and the time come after the bar's container, which is the first child
    lv_obj_set_style_border_color(lv_obj_get_child(row, 0), color, LV_STATE_DEFAULT);
}

static void onRowStateChanged(lv_event_t* event) {
    updateBarColor(lv_event_get_target_obj(event));
}

static void onKey(lv_event_t* event) {
    if (lv_event_get_key(event) == LV_KEY_ESC) {
        window_manager_overlay_hide();
    }
}

static void onRowClicked(lv_event_t* event) {
    const auto id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    // The action only forwards the request to a task, as this is the LVGL task
    const bool acted = notification_invoke_action(id) == ERROR_NONE;
    // A sticky notification stays until its sender dismisses it. The panel notices the event and rebuilds its list.
    notification_dismiss_unless_sticky(id);
    if (acted) {
        // The action usually opens an app, which the menu would cover
        window_manager_overlay_hide();
    }
}

static void createRow(NotificationPanel* panel, const Notification& notification) {
    auto* row = lvgl_list_add_button(panel->list, nullptr, nullptr);
    lv_obj_set_user_data(row, reinterpret_cast<void*>(static_cast<uintptr_t>(notification.id)));
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_event_cb(row, onRowClicked, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(row, onKey, LV_EVENT_KEY, nullptr);

    auto* texts = lv_obj_create(row);
    lv_obj_remove_style_all(texts);
    // A touch must reach the row, which is what handles the click
    lv_obj_set_clickable(texts, false);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(texts, 0, LV_STATE_DEFAULT);
    // The bar is the left border of the texts, like in the mesh chat
    lv_obj_set_style_border_width(texts, LV_DPX(3), LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(texts, LV_BORDER_SIDE_LEFT, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(texts, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(texts, LV_DPX(6), LV_STATE_DEFAULT);
    updateBarColor(row);
    lv_obj_add_event_cb(row, onRowStateChanged, LV_EVENT_STATE_CHANGED, nullptr);

    // The sender comes first, in small text
    auto* source = lv_label_create(texts);
    lv_obj_set_width(source, LV_PCT(100));
    lv_obj_set_style_text_font(source, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    // A dimmed text can't be read on a monochrome display, certainly not when the row is selected and its colors are inverted
    if (!lvgl_theme_is_mono()) {
        lv_obj_set_style_text_opa(source, LV_OPA_70, LV_STATE_DEFAULT);
    }
    lv_label_set_long_mode(source, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(source, getNotificationSourceName(notification.source_id).c_str());

    const bool hasTitle = notification.title[0] != '\0';
    if (hasTitle) {
        auto* title = lv_label_create(texts);
        lv_obj_set_width(title, LV_PCT(100));
        lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
        lv_label_set_text(title, notification.title);
    }
    // Without a title the message is the main text
    auto* message = lv_label_create(texts);
    lv_obj_set_width(message, LV_PCT(100));
    if (hasTitle) {
        lv_obj_set_style_text_font(message, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    }
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(message, notification.message);

    if (notification.sticky) {
        // It stays until the sender resolves it
        auto* pin = lv_label_create(row);
        lv_obj_set_style_text_font(pin, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_label_set_text(pin, LVGL_ICON_SHARED_PUSH_PIN);
    }

    auto* time = lv_label_create(row);
    lv_obj_set_style_text_font(time, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
    Row info = { time, notification.uptime_ms, formatAge(notification.uptime_ms) };
    lv_label_set_text(time, info.ageText.c_str());
    panel->rows.push_back(info);
}

/** Reads the notifications and rebuilds the list, keeping its scroll position */
static void rebuild(NotificationPanel* panel) {
    size_t count = 0;
    panel->notifications.resize(NOTIFICATION_HISTORY_CAPACITY);
    if (notification_get_all(panel->notifications.data(), panel->notifications.size(), &count) != ERROR_NONE) {
        // Showing nothing would be wrong, so the rows stay and the next timer tick tries again
        panel->retryRead = true;
        return;
    }
    panel->retryRead = false;

    const int32_t scrollY = lv_obj_get_scroll_y(panel->list);
    lv_obj_clean(panel->list);
    panel->rows.clear();
    size_t clearable = 0;
    // The sticky notifications are on top, and both groups are newest first
    for (const bool sticky : { true, false }) {
        for (size_t i = 0; i < count; i++) {
            if (panel->notifications[i].sticky != sticky) {
                continue;
            }
            if (!sticky) {
                clearable++;
            }
            createRow(panel, panel->notifications[i]);
        }
    }
    for (size_t i = 0; i < count; i++) {
        // The rows copied what they show
        notification_release(&panel->notifications[i]);
    }
    if (count > 0) {
        lvgl_list_hide_last_divider(panel->list);
    }
    lv_obj_set_hidden(panel->emptyLabel, count > 0);
    lv_obj_set_hidden(panel->list, count == 0);
    if (panel->onCount != nullptr) {
        panel->onCount(panel->userData, count, clearable);
    }
    lv_obj_update_layout(panel->list);
    lv_obj_scroll_to_y(panel->list, scrollY, LV_ANIM_OFF);
}

static void onTimer(lv_timer_t* timer) {
    auto* panel = static_cast<NotificationPanel*>(lv_timer_get_user_data(timer));
    bool changed = false;
    NotificationEvent event;
    while (notification_poll(&panel->subscription, &event) == ERROR_NONE) {
        changed = true;
    }
    if (changed || panel->retryRead) {
        rebuild(panel);
        return;
    }
    for (auto& row : panel->rows) {
        tt::std::string age = formatAge(row.uptimeMs);
        if (age != row.ageText) {
            row.ageText = age;
            lv_label_set_text(row.timeLabel, row.ageText.c_str());
        }
    }
}

NotificationPanel* notificationPanelCreate(lv_obj_t* parent, NotificationCountCallback onCount, void* userData) {
    auto* panel = tt::newObject<NotificationPanel>();
    if (panel == nullptr) {
        return nullptr;
    }
    panel->onCount = onCount;
    panel->userData = userData;

    task_event_group_construct(&panel->eventGroup);
    panel->subscribed = notification_subscribe(&panel->subscription, &panel->eventGroup) == ERROR_NONE;
    if (!panel->subscribed) {
        LOG_W(TAG, "Not subscribed, the list only updates when it is opened");
    }

    panel->root = lv_obj_create(parent);
    lv_obj_remove_style_all(panel->root);
    lv_obj_set_size(panel->root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(panel->root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(panel->root, false);
    lv_obj_set_style_pad_row(panel->root, 4, LV_STATE_DEFAULT);

    panel->list = lvgl_list_create(panel->root);
    lv_obj_set_width(panel->list, LV_PCT(100));
    // Only the list scrolls, and only vertically
    lv_obj_set_scroll_dir(panel->list, LV_DIR_VER);

    // Takes the place of the list while it is empty, and centers its text in that space
    panel->emptyLabel = lv_obj_create(panel->root);
    lv_obj_remove_style_all(panel->emptyLabel);
    lv_obj_set_size(panel->emptyLabel, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(panel->emptyLabel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel->emptyLabel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(panel->emptyLabel, false);
    auto* emptyText = lv_label_create(panel->emptyLabel);
    lv_obj_set_width(emptyText, LV_PCT(100));
    lv_obj_set_style_text_align(emptyText, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
    lv_label_set_text(emptyText, tr("No notifications"));

    rebuild(panel);
    panel->timer = lv_timer_create(onTimer, UPDATE_INTERVAL_MS, panel);
    return panel;
}

lv_obj_t* notificationPanelGetRoot(const NotificationPanel* panel) {
    return panel->root;
}

void notificationPanelFit(NotificationPanel* panel, int32_t maxHeight) {
    lv_obj_set_height(panel->root, LV_SIZE_CONTENT);
    lv_obj_set_height(panel->list, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(panel->list, 0);
    lv_obj_update_layout(panel->root);
    if (lv_obj_get_height(panel->root) > maxHeight) {
        lv_obj_set_height(panel->root, maxHeight);
        lv_obj_set_height(panel->list, 0);
        lv_obj_set_flex_grow(panel->list, 1);
    }
}

void notificationPanelDestroy(NotificationPanel* panel) {
    if (panel->timer != nullptr) {
        lv_timer_delete(panel->timer);
    }
    if (panel->subscribed) {
        notification_unsubscribe(&panel->subscription);
    }
    task_event_group_destruct(&panel->eventGroup);
    tt::deleteObject(panel);
}

}
