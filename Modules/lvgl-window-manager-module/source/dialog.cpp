// SPDX-License-Identifier: Apache-2.0
#include <lvgl_window_manager/dialog.h>
#include <lvgl_window_manager/window_manager.h>
#include "system_bars_internal.h"
#include "window_manager_internal.h"

#include <lvgl/devices/indev.h>
#include <lvgl/devices/keyboard.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/lvgl.h>
#include <lvgl/widgets/card.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/list.h>

#include <tactility/Memory.hpp>
#include <tactility/std/string.hpp>
#include <tactility/std/vector.hpp>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/delay.h>
#include <tactility/freertos/semphr.h>
#include <tactility/log.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <string>
#include <vector>

constexpr auto* TAG = "dialog";

namespace {

/** The task builds the widgets, waits for the dialog to close, and calls the result function. Its stack is in internal RAM, so the result function may access files. */
constexpr size_t DIALOG_TASK_STACK_SIZE = 6144;
/** The narrowest landscape display that gets a narrower dialog, in pixels */
constexpr int32_t WIDE_DISPLAY_MIN_WIDTH = 600;
constexpr TickType_t WAIT_FOR_APP_TIMEOUT_MS = 3000;
/** Content dialogs fill displays that are lower than this, in pixels */
constexpr int32_t COMPACT_DISPLAY_HEIGHT = 400;

enum class DialogKind { Alert, Input, Selection, Content };

struct Dialog {
    DialogKind kind = DialogKind::Alert;
    AppInstanceId appInstanceId = 0;
    std::string title;
    std::string message;
    std::string prefilled;
    std::vector<std::string> buttonLabels;
    /** The icon glyphs of the selection items, empty for none. Either empty or one per item. */
    tt::std::vector<tt::std::string> itemIcons;
    WindowAlertDialogResultFn onAlertResult = nullptr;
    WindowInputDialogResultFn onInputResult = nullptr;
    WindowSelectionDialogResultFn onSelectionResult = nullptr;
    void* userData = nullptr;

    // Only for DialogKind::Content
    DialogContentPtr content;
    /** Set when the content has work for the dialog task */
    std::atomic<bool> workPending { false };
    /** The hidden backdrop of a closed content dialog, which the dialog task deletes. Guarded by the LVGL lock. */
    lv_obj_t* retired = nullptr;
    /** What the page bar did before the dialog, and whether content was built. Guarded by the LVGL lock. */
    SystemBarsPageState pageState {};
    bool contentBuilt = false;

    Thread* thread = nullptr;
    /** Given by the close path, so the task can continue */
    SemaphoreHandle_t closedSignal = nullptr;
    /** Set by dialog_cancel_for_app(), read by the task before it calls the result function */
    std::atomic<bool> canceled { false };

    // Only used with the LVGL lock held
    lv_obj_t* backdrop = nullptr;
    /** The widget of the window of the app, of which the deletion closes the dialog */
    lv_obj_t* appWidget = nullptr;
    lv_obj_t* textarea = nullptr;
    lv_group_t* group = nullptr;
    bool shown = false;
    bool closed = false;

    int32_t result = WINDOW_DIALOG_DISMISSED;
    bool confirmed = false;
    std::string text;
};

struct DialogManager {
    /** Guards the lists, and is only held shortly */
    Mutex mutex {};
    /** The dialog that is shown, which is guarded by the LVGL lock instead */
    Dialog* shown = nullptr;
    /** Admitted dialogs whose startup or task has not finished */
    tt::std::vector<Dialog*> active;
    /** Threads that have finished, as a thread can't free itself */
    tt::std::vector<Thread*> finishedThreads;
    /** Apps whose dialogs are canceled during an active window removal */
    tt::std::vector<AppInstanceId> canceledApps;
    /** Active window removals, including removals temporarily waiting without lifecycle_mutex */
    tt::std::vector<AppInstanceId> removingApps;

    DialogManager() { mutex_construct(&mutex); }
};

DialogManager& manager() {
    static DialogManager* instance = tt::newObject<DialogManager>();
    check(instance != nullptr);
    return *instance;
}

bool app_is_canceled_locked(const DialogManager& m, AppInstanceId app_instance_id) {
    return std::find(m.canceledApps.begin(), m.canceledApps.end(), app_instance_id) != m.canceledApps.end();
}

bool app_is_removing_locked(const DialogManager& m, AppInstanceId app_instance_id) {
    return std::find(m.removingApps.begin(), m.removingApps.end(), app_instance_id) != m.removingApps.end();
}

void allow_app_dialogs_locked(DialogManager& m, AppInstanceId app_instance_id) {
    if (app_is_removing_locked(m, app_instance_id)) {
        return;
    }
    m.canceledApps.erase(std::remove(m.canceledApps.begin(), m.canceledApps.end(), app_instance_id), m.canceledApps.end());
}

// region Closing

void on_backdrop_deleted(lv_event_t* event);
void on_backdrop_clicked(lv_event_t* event);
void on_app_widget_deleted(lv_event_t* event);

void delete_backdrop(void* backdrop) {
    lv_obj_delete(static_cast<lv_obj_t*>(backdrop));
}

// A pending deletion must not run for a backdrop that was deleted along with the window
void cancel_backdrop_deletion(lv_event_t* event) {
    lv_async_call_cancel(delete_backdrop, lv_event_get_current_target_obj(event));
}

void on_retired_deleted(lv_event_t* event) {
    static_cast<Dialog*>(lv_event_get_user_data(event))->retired = nullptr;
}

// Call with the LVGL lock held
void close_dialog_locked(Dialog* dialog, int32_t result, bool confirmed) {
    if (dialog->closed) {
        return;
    }
    dialog->closed = true;
    dialog->result = result;
    dialog->confirmed = confirmed;
    if (confirmed && dialog->textarea != nullptr) {
        dialog->text = lv_textarea_get_text(dialog->textarea);
    }

    if (dialog->contentBuilt) {
        dialog->contentBuilt = false;
        dialog->content->detach();
        // Does nothing when the window of the app was deleted since, as what it set is gone then
        system_bars_page_state_restore(&dialog->pageState);
    }

    if (dialog->group != nullptr) {
        // Another layer can have taken over the keyboard since, which must keep it
        if (lv_group_get_default() == dialog->group) {
            lvgl_keyboard_set_active_group(nullptr);
        }
        lv_group_delete(dialog->group);
        dialog->group = nullptr;
    }
    if (dialog->appWidget != nullptr) {
        lv_obj_remove_event_cb_with_user_data(dialog->appWidget, on_app_widget_deleted, dialog);
        dialog->appWidget = nullptr;
    }
    if (dialog->backdrop != nullptr) {
        // The dialog is freed after this, so the backdrop must no longer refer to it
        lv_obj_remove_event_cb_with_user_data(dialog->backdrop, on_backdrop_deleted, dialog);
        lv_obj_remove_event_cb_with_user_data(dialog->backdrop, on_backdrop_clicked, dialog);
        // Deleted asynchronously, as this may run inside an event of one of its widgets
        lv_obj_set_hidden(dialog->backdrop, true);
        if (dialog->kind == DialogKind::Content) {
            // The widgets refer to the content, so the dialog task deletes them before it frees the content
            dialog->retired = dialog->backdrop;
            lv_obj_add_event_cb(dialog->backdrop, on_retired_deleted, LV_EVENT_DELETE, dialog);
        } else {
            lv_obj_add_event_cb(dialog->backdrop, cancel_backdrop_deletion, LV_EVENT_DELETE, nullptr);
            lv_async_call(delete_backdrop, dialog->backdrop);
        }
        dialog->backdrop = nullptr;
    }
    dialog->textarea = nullptr;

    if (manager().shown == dialog) {
        manager().shown = nullptr;
    }
    dialog->shown = false;
    xSemaphoreGive(dialog->closedSignal);
}

void on_backdrop_clicked(lv_event_t* event) {
    if (lv_event_get_target_obj(event) == lv_event_get_current_target_obj(event)) {
        close_dialog_locked(static_cast<Dialog*>(lv_event_get_user_data(event)), WINDOW_DIALOG_DISMISSED, false);
    }
}

void on_key(lv_event_t* event) {
    if (lv_event_get_key(event) == LV_KEY_ESC) {
        close_dialog_locked(static_cast<Dialog*>(lv_event_get_user_data(event)), WINDOW_DIALOG_DISMISSED, false);
    }
}

/** The widgets are deleted along with the window of the app, e.g. when another app's window opens */
void on_backdrop_deleted(lv_event_t* event) {
    auto* dialog = static_cast<Dialog*>(lv_event_get_user_data(event));
    // Already being deleted, so it is neither hidden nor deleted again
    dialog->backdrop = nullptr;
    dialog->textarea = nullptr;
    close_dialog_locked(dialog, WINDOW_DIALOG_DISMISSED, false);
}

/** The window of the app is deleted, e.g. when another app's window opens. The backdrop is not a child of it, so it is closed here. */
void on_app_widget_deleted(lv_event_t* event) {
    auto* dialog = static_cast<Dialog*>(lv_event_get_user_data(event));
    // Already being deleted
    dialog->appWidget = nullptr;
    close_dialog_locked(dialog, WINDOW_DIALOG_DISMISSED, false);
}

struct ButtonData {
    Dialog* dialog;
    int32_t index;
};

void on_button_pressed(lv_event_t* event) {
    auto* data = static_cast<ButtonData*>(lv_event_get_user_data(event));
    close_dialog_locked(data->dialog, data->index, data->dialog->kind == DialogKind::Input && data->index == 0);
}

void on_close_pressed(lv_event_t* event) {
    close_dialog_locked(static_cast<Dialog*>(lv_event_get_user_data(event)), WINDOW_DIALOG_DISMISSED, false);
}

/** Scrolls the message by most of its visible height. A positive direction shows the text before the visible part. */
void scroll_message(lv_event_t* event, int32_t direction) {
    auto* message_area = static_cast<lv_obj_t*>(lv_event_get_user_data(event));
    lv_obj_scroll_by_bounded(message_area, 0, direction * lv_obj_get_height(message_area) * 4 / 5, LV_ANIM_ON);
}

void on_scroll_up_pressed(lv_event_t* event) { scroll_message(event, 1); }
void on_scroll_down_pressed(lv_event_t* event) { scroll_message(event, -1); }

lv_obj_t* create_icon_button(lv_obj_t* parent, const char* icon) {
    lv_obj_t* button = lvgl_icon_button_create(parent);
    lv_obj_t* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, icon);
    return button;
}

void on_button_deleted(lv_event_t* event) {
    delete static_cast<ButtonData*>(lv_event_get_user_data(event));
}

void on_textarea_ready(lv_event_t* event) {
    close_dialog_locked(static_cast<Dialog*>(lv_event_get_user_data(event)), 0, true);
}

// endregion

// region Widgets

lv_obj_t* create_button(Dialog* dialog, lv_obj_t* parent, const std::string& text, int32_t index) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_t* label = lv_label_create(button);
    lv_obj_center(label);
    lv_label_set_text(label, text.c_str());
    auto* data = new(std::nothrow) ButtonData { dialog, index };
    if (data != nullptr) {
        lv_obj_add_event_cb(button, on_button_pressed, LV_EVENT_SHORT_CLICKED, data);
        lv_obj_add_event_cb(button, on_button_deleted, LV_EVENT_DELETE, data);
    }
    return button;
}

/**
 * Builds the backdrop and the card over the widget of the app, and makes the dialog's own group the active one.
 * @return false when the app has no topmost window
 * @warning Caller must hold the LVGL lock
 */
bool build_dialog_locked(Dialog* dialog) {
    lv_obj_t* app_widget = window_manager_get_app_widget_locked(dialog->appInstanceId);
    // The backdrop is placed in the container of the windows, so it covers everything that the app has, also its widgets that are laid out by the app itself
    lv_obj_t* container = window_manager_get_content_widget_locked();
    if (app_widget == nullptr || container == nullptr || window_manager_get_active_layer() == WINDOW_LAYER_OVERLAY) {
        return false;
    }
    lv_group_t* group = lv_group_create();
    if (group == nullptr) {
        return false;
    }
    dialog->group = group;
    // The app's widgets stay in the shared group, which no longer receives the keyboard. Widgets created from here on join the dialog's group.
    lvgl_keyboard_set_active_group(group);

    lv_display_t* display = lv_obj_get_display(container);
    const int32_t display_width = lv_display_get_horizontal_resolution(display);
    const int32_t display_height = lv_display_get_vertical_resolution(display);
    const bool wide = display_width > display_height && display_width >= WIDE_DISPLAY_MIN_WIDTH;

    lv_obj_t* backdrop = lv_obj_create(container);
    dialog->backdrop = backdrop;
    dialog->appWidget = app_widget;
    lv_obj_add_event_cb(app_widget, on_app_widget_deleted, LV_EVENT_DELETE, dialog);
    // The container can be a layout, which must not place the backdrop next to the window
    lv_obj_set_floating(backdrop, true);
    lv_obj_align(backdrop, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(backdrop, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(backdrop, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(backdrop, 0, LV_STATE_DEFAULT);
    // Monochrome displays can't dim the app, so the dialog draws over it as-is
    const bool monochrome = lv_display_get_color_format(display) == LV_COLOR_FORMAT_I1;
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(backdrop, monochrome ? LV_OPA_TRANSP : LV_OPA_50, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(backdrop, false);
    lv_obj_add_event_cb(backdrop, on_backdrop_clicked, LV_EVENT_CLICKED, dialog);
    lv_obj_add_event_cb(backdrop, on_backdrop_deleted, LV_EVENT_DELETE, dialog);

    if (dialog->kind == DialogKind::Content) {
        // The content fills the card. Small displays can't spare a margin.
        const bool compact = display_height < COMPACT_DISPLAY_HEIGHT;
        // A plain object instead of a card: it has the background of the app's windows and of toolbars, and the content has its own
        lv_obj_t* content_card = lv_obj_create(backdrop);
        lv_obj_set_size(content_card, lv_pct(compact ? 100 : (wide ? 80 : 95)), lv_pct(compact ? 100 : 90));
        lv_obj_center(content_card);
        lv_obj_set_flex_flow(content_card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_all(content_card, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(content_card, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_clip_corner(content_card, true, LV_STATE_DEFAULT);
        if (compact) {
            lv_obj_set_style_radius(content_card, 0, LV_STATE_DEFAULT);
        }
        lv_obj_set_scrollable(content_card, false);
        system_bars_page_state_take(&dialog->pageState);
        dialog->contentBuilt = true;
        dialog->content->build(dialog, content_card, group);
        const uint32_t content_count = lv_group_get_obj_count(group);
        for (uint32_t i = 0; i < content_count; i++) {
            lv_obj_add_event_cb(lv_group_get_obj_by_index(group, static_cast<int32_t>(i)), on_key, LV_EVENT_KEY, dialog);
        }
        if (lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
            lvgl_focus_hide_key_selection(group);
        }
        return true;
    }

    lv_obj_t* card = lvgl_card_create(backdrop);
    lv_obj_set_width(card, lv_pct(wide ? 60 : 90));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(card, display_height * 90 / 100, LV_STATE_DEFAULT);
    lv_obj_center(card);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(card, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(card, false);

    // The selection dialog has a close button, as devices without a touchscreen have no other way to dismiss it than the Escape key.
    // The alert and input dialogs have buttons that close them.
    const bool has_close_button = dialog->kind == DialogKind::Selection;
    lv_obj_t* title_row = lv_obj_create(card);
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_row, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(title_row, false);
    if (has_close_button) {
        lv_obj_t* close_button = lvgl_icon_button_create(title_row);
        lv_obj_t* close_icon = lv_label_create(close_button);
        lv_obj_set_style_text_font(close_icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
        lv_label_set_text(close_icon, LVGL_ICON_SHARED_CLOSE);
        lv_obj_add_event_cb(close_button, on_close_pressed, LV_EVENT_SHORT_CLICKED, dialog);
        // A plain button ignores left and right, so it is a grid navigation group of its own, like the other widgets of the dialog
        lvgl_grid_navigation_add(title_row);
    }

    lv_obj_t* title = lv_label_create(title_row);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_LARGE), LV_STATE_DEFAULT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(title, dialog->title.c_str());

    // A message that doesn't fit is scrolled with buttons at the right of the title. They are removed again when the message fits.
    lv_obj_t* scroll_up_button = nullptr;
    lv_obj_t* scroll_down_button = nullptr;
    if (dialog->kind == DialogKind::Alert) {
        scroll_up_button = create_icon_button(title_row, LVGL_ICON_SHARED_KEYBOARD_ARROW_UP);
        scroll_down_button = create_icon_button(title_row, LVGL_ICON_SHARED_KEYBOARD_ARROW_DOWN);
        lvgl_grid_navigation_add(title_row);
    }

    if (dialog->kind == DialogKind::Selection) {
        // The items scroll when they don't fit. The card provides the background.
        lv_obj_t* list = lvgl_list_create(card);
        lv_obj_set_width(list, LV_PCT(100));
        lv_obj_set_height(list, LV_SIZE_CONTENT);
        lv_obj_set_style_max_height(list, display_height * 60 / 100, LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(list, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(list, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_clip_corner(card, true, LV_STATE_DEFAULT);
        lv_obj_t* first_item = nullptr;
        for (size_t i = 0; i < dialog->buttonLabels.size(); i++) {
            lv_obj_t* item = lvgl_list_add_button(list, nullptr, dialog->buttonLabels[i].c_str());
            if (i < dialog->itemIcons.size() && !dialog->itemIcons[i].empty()) {
                lv_obj_t* icon = lv_label_create(item);
                lv_obj_set_style_text_font(icon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
                lv_label_set_text(icon, dialog->itemIcons[i].c_str());
                lv_obj_move_to_index(icon, 0);
            }
            auto* data = new(std::nothrow) ButtonData { dialog, static_cast<int32_t>(i) };
            if (data != nullptr) {
                lv_obj_add_event_cb(item, on_button_pressed, LV_EVENT_SHORT_CLICKED, data);
                lv_obj_add_event_cb(item, on_button_deleted, LV_EVENT_DELETE, data);
            }
            if (first_item == nullptr) {
                first_item = item;
            }
        }
        lvgl_list_hide_last_divider(list);
        // A list that scrolls gets the regular font for the title, which leaves more room for the items
        lv_obj_update_layout(card);
        if (lv_obj_get_scroll_top(list) + lv_obj_get_scroll_bottom(list) > 0) {
            lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_DEFAULT), LV_STATE_DEFAULT);
        }
        // The arrow keys move between the items
        lvgl_grid_navigation_add(list);
        const uint32_t object_count = lv_group_get_obj_count(group);
        for (uint32_t i = 0; i < object_count; i++) {
            lv_obj_add_event_cb(lv_group_get_obj_by_index(group, static_cast<int32_t>(i)), on_key, LV_EVENT_KEY, dialog);
        }
        if (first_item != nullptr && !lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
            lv_group_focus_obj(list);
            lv_gridnav_set_focused(list, first_item, LV_ANIM_OFF);
        }
        if (lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
            lvgl_focus_hide_key_selection(group);
        }
        return true;
    }

    // The message scrolls when it doesn't fit, and can be selected so the keys can scroll it
    lv_obj_t* message_area = lv_obj_create(card);
    lv_obj_remove_style_all(message_area);
    lv_obj_set_width(message_area, LV_PCT(100));
    lv_obj_set_height(message_area, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(message_area, display_height * 40 / 100, LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(message_area, LV_DIR_VER);
    lv_obj_set_clickable(message_area, true);
    lv_obj_set_click_focusable(message_area, true);
    lv_group_add_obj(group, message_area);
    lv_obj_t* message = lv_label_create(message_area);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(message, dialog->message.c_str());
    if (scroll_up_button != nullptr) {
        lv_obj_add_event_cb(scroll_up_button, on_scroll_up_pressed, LV_EVENT_SHORT_CLICKED, message_area);
        lv_obj_add_event_cb(scroll_down_button, on_scroll_down_pressed, LV_EVENT_SHORT_CLICKED, message_area);
    }

    if (dialog->kind == DialogKind::Input) {
        lv_obj_t* textarea = lv_textarea_create(card);
        dialog->textarea = textarea;
        // Creating a textarea can add it to the shared keyboard group of the apps, which isn't the active group now
        lv_group_remove_obj(textarea);
        lv_group_add_obj(group, textarea);
        lv_obj_set_width(textarea, LV_PCT(100));
        lv_textarea_set_one_line(textarea, true);
        lv_obj_add_event_cb(textarea, on_textarea_ready, LV_EVENT_READY, dialog);
        if (!dialog->prefilled.empty()) {
            lv_textarea_set_text(textarea, dialog->prefilled.c_str());
        }
    }

    lv_obj_t* buttons = lv_obj_create(card);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(buttons, LV_DPX(8), LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(buttons, LV_DPX(4), LV_STATE_DEFAULT);
    lv_obj_set_scrollable(buttons, false);
    lv_obj_t* first_button = nullptr;
    for (size_t i = 0; i < dialog->buttonLabels.size(); i++) {
        lv_obj_t* button = create_button(dialog, buttons, dialog->buttonLabels[i], static_cast<int32_t>(i));
        if (first_button == nullptr) {
            first_button = button;
        }
    }
    // The arrow keys move between the buttons as one row
    lvgl_grid_navigation_add(buttons);

    if (scroll_up_button != nullptr) {
        // The card has its final size once the layout is updated
        lv_obj_update_layout(card);
        const auto overflows = [message_area]() { return lv_obj_get_scroll_top(message_area) + lv_obj_get_scroll_bottom(message_area) > 0; };
        if (overflows()) {
            // A message that doesn't fit gets the small font, and the title the regular font, which leaves more room for the message
            lv_obj_set_style_text_font(message, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(title, lvgl_get_text_font(FONT_SIZE_DEFAULT), LV_STATE_DEFAULT);
            lv_obj_update_layout(card);
        }
        if (overflows()) {
            // The scroll bar is always visible while there is more to read
            lv_obj_set_scrollbar_mode(message_area, LV_SCROLLBAR_MODE_ON);
        } else {
            lvgl_grid_navigation_remove(title_row);
            // The row stays in the group after that, which would leave an empty stop for the keys
            lv_group_remove_obj(title_row);
            lv_obj_delete(scroll_up_button);
            lv_obj_delete(scroll_down_button);
        }
    }

    // The Escape key closes the dialog from any of its widgets
    const uint32_t object_count = lv_group_get_obj_count(group);
    for (uint32_t i = 0; i < object_count; i++) {
        lv_obj_add_event_cb(lv_group_get_obj_by_index(group, static_cast<int32_t>(i)), on_key, LV_EVENT_KEY, dialog);
    }

    // The text field or the first button gets the focus. With a pointer device, it doesn't show as selected.
    if (dialog->textarea != nullptr) {
        lv_group_focus_obj(dialog->textarea);
    } else if (first_button != nullptr) {
        lv_group_focus_obj(buttons);
        lv_gridnav_set_focused(buttons, first_button, LV_ANIM_OFF);
    }
    if (lvgl_indev_exists(LV_INDEV_TYPE_POINTER)) {
        lvgl_focus_hide_key_selection(group);
    }
    return true;
}

// endregion

// region Task

void reap_finished_threads() {
    auto& m = manager();
    std::vector<Thread*> to_free;
    mutex_lock(&m.mutex);
    for (auto it = m.finishedThreads.begin(); it != m.finishedThreads.end();) {
        if (thread_get_state(*it) == THREAD_STATE_STOPPED) {
            to_free.push_back(*it);
            it = m.finishedThreads.erase(it);
        } else {
            ++it;
        }
    }
    mutex_unlock(&m.mutex);
    for (Thread* thread : to_free) {
        thread_free(thread);
    }
}

int32_t dialog_main(void* context) {
    auto* dialog = static_cast<Dialog*>(context);

    lvgl_lock();
    // With one item there is nothing to choose, so the result is that item
    const bool automatic = dialog->kind == DialogKind::Selection && dialog->buttonLabels.size() == 1;
    // A dismissal can come before the widgets are built
    const bool built = !dialog->closed && !automatic && build_dialog_locked(dialog);
    if (built) {
        dialog->shown = true;
    } else if (!dialog->closed) {
        if (automatic) {
            dialog->result = 0;
        }
        if (dialog->group != nullptr) {
            lv_group_delete(dialog->group);
            dialog->group = nullptr;
        }
        dialog->closed = true;
        if (manager().shown == dialog) {
            manager().shown = nullptr;
        }
        xSemaphoreGive(dialog->closedSignal);
    }
    lvgl_unlock();

    // The signal is given when the dialog closes, and when the content has work
    bool closed = false;
    while (!closed) {
        xSemaphoreTake(dialog->closedSignal, portMAX_DELAY);
        if (dialog->workPending.exchange(false)) {
            dialog->content->work();
        }
        lvgl_lock();
        closed = dialog->closed;
        lvgl_unlock();
    }

    if (dialog->content != nullptr) {
        lvgl_lock();
        if (dialog->retired != nullptr) {
            lv_obj_delete(dialog->retired);
        }
        lvgl_unlock();
        dialog->content->release();
    }

    if (!dialog->canceled.load()) {
        if (dialog->kind == DialogKind::Alert) {
            if (dialog->onAlertResult != nullptr) {
                dialog->onAlertResult(dialog->result, dialog->userData);
            }
        } else if (dialog->kind == DialogKind::Selection) {
            if (dialog->onSelectionResult != nullptr) {
                dialog->onSelectionResult(dialog->result, dialog->userData);
            }
        } else if (dialog->kind == DialogKind::Content) {
            dialog->content->deliver(dialog->confirmed);
        } else if (dialog->onInputResult != nullptr) {
            dialog->onInputResult(dialog->confirmed, dialog->confirmed ? dialog->text.c_str() : "", dialog->userData);
        }
    }

    auto& m = manager();
    Thread* thread = dialog->thread;
    mutex_lock(&m.mutex);
    m.active.erase(std::remove(m.active.begin(), m.active.end(), dialog), m.active.end());
    m.finishedThreads.push_back(thread);
    mutex_unlock(&m.mutex);
    vSemaphoreDelete(dialog->closedSignal);
    tt::deleteObject(dialog);
    return 0;
}

// endregion

/**
 * Starts the task of a dialog that is filled in. Takes over the dialog: it is freed on failure.
 */
error_t start_dialog(Dialog* dialog) {
    auto& m = manager();
    reap_finished_threads();

    dialog->closedSignal = xSemaphoreCreateBinary();
    if (dialog->closedSignal == nullptr) {
        tt::deleteObject(dialog);
        return ERROR_OUT_OF_MEMORY;
    }

    lvgl_lock();
    mutex_lock(&m.mutex);
    const bool has_window = window_manager_get_app_widget_locked(dialog->appInstanceId) != nullptr;
    const bool overlay_shown = window_manager_get_active_layer() == WINDOW_LAYER_OVERLAY;
    // The slot is claimed here, so a second call is refused right away
    const bool busy = m.shown != nullptr;
    const bool canceled = app_is_canceled_locked(m, dialog->appInstanceId);
    // Keep rejecting windowless apps independently of cancellation bookkeeping.
    if (has_window && !overlay_shown && !busy && !canceled) {
        m.shown = dialog;
        // Cancellation uses the same mutex, so it cannot pass a validated but unregistered dialog
        // even if the window is invalidated before removal takes the LVGL lock.
        m.active.push_back(dialog);
    }
    mutex_unlock(&m.mutex);
    lvgl_unlock();
    if (!has_window || overlay_shown || busy || canceled) {
        vSemaphoreDelete(dialog->closedSignal);
        tt::deleteObject(dialog);
        return has_window && !canceled ? ERROR_RESOURCE_BUSY : ERROR_INVALID_STATE;
    }

    auto rollback_admission = [&m, dialog]() {
        lvgl_lock();
        if (m.shown == dialog) {
            m.shown = nullptr;
        }
        lvgl_unlock();
        mutex_lock(&m.mutex);
        m.active.erase(std::remove(m.active.begin(), m.active.end(), dialog), m.active.end());
        mutex_unlock(&m.mutex);
    };

    dialog->thread = thread_alloc_full("dialog", DIALOG_TASK_STACK_SIZE, dialog_main, dialog, -1);
    if (dialog->thread == nullptr) {
        rollback_admission();
        vSemaphoreDelete(dialog->closedSignal);
        tt::deleteObject(dialog);
        return ERROR_OUT_OF_MEMORY;
    }

    if (thread_start(dialog->thread) != ERROR_NONE) {
        rollback_admission();
        vSemaphoreDelete(dialog->closedSignal);
        thread_free(dialog->thread);
        tt::deleteObject(dialog);
        LOG_E(TAG, "Failed to start the dialog task");
        return ERROR_OUT_OF_MEMORY;
    }
    return ERROR_NONE;
}

} // namespace

// region Internal

error_t dialog_start_content(AppInstanceId app_instance_id, DialogContentPtr content) {
    auto* dialog = tt::newObject<Dialog>();
    if (dialog == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    dialog->kind = DialogKind::Content;
    dialog->appInstanceId = app_instance_id;
    dialog->content = std::move(content);
    return start_dialog(dialog);
}

void dialog_content_post_work(void* handle) {
    auto* dialog = static_cast<Dialog*>(handle);
    dialog->workPending.store(true);
    xSemaphoreGive(dialog->closedSignal);
}

void dialog_content_close_locked(void* handle, bool confirmed) {
    close_dialog_locked(static_cast<Dialog*>(handle), confirmed ? 0 : WINDOW_DIALOG_DISMISSED, confirmed);
}

void dialog_allow_for_app(AppInstanceId app_instance_id) {
    auto& m = manager();
    mutex_lock(&m.mutex);
    allow_app_dialogs_locked(m, app_instance_id);
    mutex_unlock(&m.mutex);
}

bool dialog_is_app_canceled(AppInstanceId app_instance_id) {
    auto& m = manager();
    mutex_lock(&m.mutex);
    const bool canceled = app_is_canceled_locked(m, app_instance_id);
    mutex_unlock(&m.mutex);
    return canceled;
}

void dialog_cancel_for_app(AppInstanceId app_instance_id) {
    auto& m = manager();
    mutex_lock(&m.mutex);
    if (!app_is_canceled_locked(m, app_instance_id)) {
        m.canceledApps.push_back(app_instance_id);
    }
    m.removingApps.push_back(app_instance_id);
    for (Dialog* dialog : m.active) {
        if (dialog->appInstanceId == app_instance_id) {
            dialog->canceled.store(true);
        }
    }
    mutex_unlock(&m.mutex);

    // A buried window has no widget deletion event to close its app's dialog.
    lvgl_lock();
    Dialog* shown = m.shown;
    if (shown != nullptr && shown->appInstanceId == app_instance_id) {
        close_dialog_locked(shown, WINDOW_DIALOG_DISMISSED, false);
    }
    lvgl_unlock();
}

void dialog_finish_remove_for_app(AppInstanceId app_instance_id) {
    auto& m = manager();
    mutex_lock(&m.mutex);
    auto removal = std::find(m.removingApps.begin(), m.removingApps.end(), app_instance_id);
    if (removal != m.removingApps.end()) {
        m.removingApps.erase(removal);
    }
    if (!app_is_removing_locked(m, app_instance_id)) {
        m.canceledApps.erase(std::remove(m.canceledApps.begin(), m.canceledApps.end(), app_instance_id), m.canceledApps.end());
    }
    mutex_unlock(&m.mutex);
}

bool dialog_wait_for_app(AppInstanceId app_instance_id) {
    auto& m = manager();
    for (uint32_t waited_ms = 0; waited_ms < WAIT_FOR_APP_TIMEOUT_MS; waited_ms += 5) {
        bool found = false;
        mutex_lock(&m.mutex);
        for (const Dialog* dialog : m.active) {
            found |= dialog->appInstanceId == app_instance_id;
        }
        mutex_unlock(&m.mutex);
        if (!found) {
            return true;
        }
        delay_millis(5);
    }
    LOG_W(TAG, "A dialog of app %u is still running", static_cast<unsigned>(app_instance_id));
    return false;
}

void dialog_reap_tasks() {
    reap_finished_threads();
}

lv_group_t* dialog_get_group_locked() {
    Dialog* shown = manager().shown;
    return shown != nullptr ? shown->group : nullptr;
}

// endregion

extern "C" {

error_t window_manager_alert_dialog(
    AppInstanceId app_instance_id,
    const char* title,
    const char* message,
    const char* const* button_labels,
    uint32_t button_count,
    WindowAlertDialogResultFn on_result,
    void* user_data
) {
    if (app_instance_id == 0 || title == nullptr || message == nullptr || button_labels == nullptr || button_count == 0) {
        return ERROR_INVALID_ARGUMENT;
    }
    auto* dialog = tt::newObject<Dialog>();
    if (dialog == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    dialog->kind = DialogKind::Alert;
    dialog->appInstanceId = app_instance_id;
    dialog->title = title;
    dialog->message = message;
    for (uint32_t i = 0; i < button_count; i++) {
        if (button_labels[i] == nullptr) {
            tt::deleteObject(dialog);
            return ERROR_INVALID_ARGUMENT;
        }
        dialog->buttonLabels.emplace_back(button_labels[i]);
    }
    dialog->onAlertResult = on_result;
    dialog->userData = user_data;
    return start_dialog(dialog);
}

error_t window_manager_input_dialog(
    AppInstanceId app_instance_id,
    const char* title,
    const char* message,
    const char* prefilled,
    const char* ok_label,
    const char* cancel_label,
    WindowInputDialogResultFn on_result,
    void* user_data
) {
    if (app_instance_id == 0 || title == nullptr || message == nullptr || ok_label == nullptr || cancel_label == nullptr) {
        return ERROR_INVALID_ARGUMENT;
    }
    auto* dialog = tt::newObject<Dialog>();
    if (dialog == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    dialog->kind = DialogKind::Input;
    dialog->appInstanceId = app_instance_id;
    dialog->title = title;
    dialog->message = message;
    dialog->prefilled = prefilled != nullptr ? prefilled : "";
    dialog->buttonLabels = { ok_label, cancel_label };
    dialog->onInputResult = on_result;
    dialog->userData = user_data;
    return start_dialog(dialog);
}

error_t window_manager_selection_dialog(
    AppInstanceId app_instance_id,
    const char* title,
    const char* const* items,
    uint32_t item_count,
    WindowSelectionDialogResultFn on_result,
    void* user_data
) {
    return window_manager_selection_dialog_with_icons(app_instance_id, title, items, nullptr, item_count, on_result, user_data);
}

error_t window_manager_selection_dialog_with_icons(
    AppInstanceId app_instance_id,
    const char* title,
    const char* const* items,
    const char* const* icons,
    uint32_t item_count,
    WindowSelectionDialogResultFn on_result,
    void* user_data
) {
    if (app_instance_id == 0 || title == nullptr || items == nullptr || item_count == 0) {
        return ERROR_INVALID_ARGUMENT;
    }
    auto* dialog = tt::newObject<Dialog>();
    if (dialog == nullptr) {
        return ERROR_OUT_OF_MEMORY;
    }
    dialog->kind = DialogKind::Selection;
    dialog->appInstanceId = app_instance_id;
    dialog->title = title;
    for (uint32_t i = 0; i < item_count; i++) {
        if (items[i] == nullptr) {
            tt::deleteObject(dialog);
            return ERROR_INVALID_ARGUMENT;
        }
        dialog->buttonLabels.emplace_back(items[i]);
        if (icons != nullptr) {
            dialog->itemIcons.emplace_back(icons[i] != nullptr ? icons[i] : "");
        }
    }
    dialog->onSelectionResult = on_result;
    dialog->userData = user_data;
    return start_dialog(dialog);
}

void window_manager_dialog_dismiss(void) {
    lvgl_lock();
    Dialog* dialog = manager().shown;
    if (dialog != nullptr) {
        close_dialog_locked(dialog, WINDOW_DIALOG_DISMISSED, false);
    }
    lvgl_unlock();
}

} // extern "C"
