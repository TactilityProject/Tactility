// SPDX-License-Identifier: Apache-2.0
#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <lvgl/widgets/statusbar.h>

#include <tactility/check.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/timer.h>
#include <tactility/log.h>
#include <tactility/system_event.h>
#include <tactility/time.h>

#include <lvgl/fonts.h>
#include <lvgl/insets.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>
#include <lvgl/icons/shared.h>
#include <lvgl/widgets/focus_container.h>
#include <lvgl/widgets/icon_button.h>

#include <tactility/Memory.hpp>

#include <algorithm>
#include <cstring>
#include <ctime>

constexpr auto* TAG = "lvgl_statusbar";
constexpr auto STATUSBAR_ICON_LIMIT = LVGL_STATUSBAR_ICON_LIMIT;

struct StatusbarIcon {
    char image[LVGL_STATUSBAR_IMAGE_MAX_LENGTH + 1] = {};
    bool visible = false;
    bool claimed = false;
    // When the icon was added: of the icons with one priority, the newest ones are dropped first when they don't fit
    uint32_t added_order = 0;
    LvglStatusbarIconPriority priority = LVGL_STATUSBAR_ICON_PRIORITY_REGULAR;
};

struct Statusbar;

struct StatusbarData {
    RecursiveMutex mutex {};
    StatusbarIcon icons[STATUSBAR_ICON_LIMIT] = {};
    Timer* time_update_timer = nullptr;
    uint8_t time_hours = 0;
    uint8_t time_minutes = 0;
    bool time_set = false;
    bool time_format_24h = true;
    uint32_t next_added_order = 0;
    void (*menu_callback)(int32_t) = nullptr;
    // The created statusbars, which are updated when the icons or the time change
    Mutex instances_mutex {};
    Statusbar* instances = nullptr;

    StatusbarData() {
        recursive_mutex_construct(&mutex);
        mutex_construct(&instances_mutex);
    }

    ~StatusbarData() {
        mutex_destruct(&instances_mutex);
        recursive_mutex_destruct(&mutex);
    }
};

// Allocated once in external memory when available
static StatusbarData& statusbar_data() {
    static StatusbarData* instance = tt::newObject<StatusbarData>();
    check(instance != nullptr);
    return *instance;
}

struct Statusbar {
    lv_obj_t obj;
    lv_obj_t* time;
    lv_obj_t* icons[STATUSBAR_ICON_LIMIT];
    lv_obj_t* battery_icon;
    // The next created statusbar
    Statusbar* next;
    // The parent of the icons, which opens the system menu
    lv_obj_t* icon_row;
    // Pushes the action container to the bottom in the side layout. Null on circular displays.
    lv_obj_t* spacer;
    // Holds the action buttons of apps in the side layout. Null on circular displays.
    lv_obj_t* action_container;
    // The horizontal padding of the split layout
    int32_t split_pad_hor;
    // False until a layout was applied once, as the first one is the split layout that the bar is created with
    bool layout_applied;
    bool side;
    /** In the side layout, the strip is on the right of the screen instead of the left */
    bool side_right;
    // Extra padding above and below the content of the strip, which keeps it clear of rounded corners
    int32_t side_inset_top;
    int32_t side_inset_bottom;
};

static void statusbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void statusbar_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void statusbar_event(const lv_obj_class_t* class_p, lv_event_t* event);

static void update_time(Statusbar* statusbar);
static void update_main(Statusbar* statusbar);
static void update_icons(Statusbar* statusbar);
static void onSideSizeChanged(lv_event_t* event);

static TickType_t getNextUpdateTime() {
    time_t now = ::time(nullptr);
    tm* tm_struct = localtime(&now);
    uint32_t seconds_to_wait = 60U - tm_struct->tm_sec;
    LOG_D(TAG, "Update in %d s", (int)seconds_to_wait);
    return pdMS_TO_TICKS(seconds_to_wait * 1000U);
}

static void notify_instances() {
    mutex_lock(&statusbar_data().instances_mutex);
    for (auto* statusbar = statusbar_data().instances; statusbar != nullptr; statusbar = statusbar->next) {
        LOG_D(TAG, "Update event");
        if (lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
            update_main(statusbar);
            lv_obj_invalidate(&statusbar->obj);
            lvgl_unlock();
        } else {
            LOG_W(TAG, "Mutex acquisition timeout (%s)", "Statusbar");
        }
    }
    mutex_unlock(&statusbar_data().instances_mutex);
}

static void on_update_time(void* /*context*/) {
    time_t now = ::time(nullptr);
    tm* tm_struct = localtime(&now);

    if (recursive_mutex_try_lock(&statusbar_data().mutex, 100 / portTICK_PERIOD_MS)) {
        bool changed = false;
        if (tm_struct->tm_year >= (2025 - 1900)) {
            statusbar_data().time_hours = tm_struct->tm_hour;
            statusbar_data().time_minutes = tm_struct->tm_min;
            statusbar_data().time_set = true;
            changed = true;

            // Reschedule
            timer_reset_with_interval(statusbar_data().time_update_timer, getNextUpdateTime());
        } else {
            timer_reset_with_interval(statusbar_data().time_update_timer, pdMS_TO_TICKS(60000U));
        }

        recursive_mutex_unlock(&statusbar_data().mutex);
        if (changed) {
            notify_instances();
        }
    }
}

static lv_obj_class_t statusbar_class = {
    .base_class = &lv_obj_class,
    .constructor_cb = &statusbar_constructor,
    .destructor_cb = &statusbar_destructor,
    .event_cb = &statusbar_event,
    .user_data = nullptr,
    .name = nullptr,
    .width_def = LV_PCT(100),
    .height_def = 20,
    .editable = false,
    .group_def = LV_OBJ_CLASS_GROUP_DEF_TRUE,
    .instance_size = sizeof(Statusbar),
    .theme_inheritable = false
};

static void onTimeChanged(struct SystemEvent* /*event*/, void* /*context*/) {
    recursive_mutex_lock(&statusbar_data().mutex);
    timer_reset_with_interval(statusbar_data().time_update_timer, 5);
    recursive_mutex_unlock(&statusbar_data().mutex);
}

static void statusbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");
    lv_obj_set_scrollable(obj, false);
    LV_TRACE_OBJ_CREATE("finished");

    // Deliberately does NOT register the instance here - that happens at the end of
    // lvgl_statusbar_create(), once statusbar->icons[] is actually populated. Registering this early
    // would let a concurrent lvgl_statusbar_icon_add()/_remove()/_set_image()/_set_visibility() call
    // from another task run update_icon() on a still-null icons[] slot before this
    // instance's own creation loop below has had a chance to fill it in.
    recursive_mutex_lock(&statusbar_data().mutex);
    if (statusbar_data().time_update_timer == nullptr) {
        statusbar_data().time_update_timer = timer_alloc(TIMER_TYPE_ONCE, 200 / portTICK_PERIOD_MS, on_update_time, nullptr);
        check(statusbar_data().time_update_timer != nullptr);
        timer_start(statusbar_data().time_update_timer);
        system_event_callback_add(KERNEL_EVENT_TIME_CHANGED, onTimeChanged, nullptr);
    }
    recursive_mutex_unlock(&statusbar_data().mutex);
}

static void statusbar_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    auto* statusbar = (Statusbar*)obj;
    mutex_lock(&statusbar_data().instances_mutex);
    for (auto** link = &statusbar_data().instances; *link != nullptr; link = &(*link)->next) {
        if (*link == statusbar) {
            *link = statusbar->next;
            break;
        }
    }
    mutex_unlock(&statusbar_data().instances_mutex);
}

static void update_icon(lv_obj_t* image, const StatusbarIcon* icon) {
    if (icon->image[0] != '\0' && icon->visible && icon->claimed) {
        lv_image_set_src(image, icon->image);
        lv_obj_set_hidden(image, false);
    } else {
        lv_obj_set_hidden(image, true);
    }
}

static void show_menu(int32_t swipe_x) {
    recursive_mutex_lock(&statusbar_data().mutex);
    auto* callback = statusbar_data().menu_callback;
    recursive_mutex_unlock(&statusbar_data().mutex);
    if (callback != nullptr) {
        callback(swipe_x);
    }
}

static void onIconRowClicked(lv_event_t* /*event*/) {
    show_menu(-1);
}

static void onIconRowKey(lv_event_t* event) {
    auto* statusbar = static_cast<Statusbar*>(lv_event_get_user_data(event));
    if (!statusbar->side) {
        return;
    }
    lv_group_t* group = lv_obj_get_group(statusbar->icon_row);
    if (group == nullptr) {
        return;
    }
    if (lv_event_get_key(event) == LV_KEY_DOWN) {
        lv_group_focus_next(group);
    } else if (lv_event_get_key(event) == LV_KEY_UP) {
        lv_group_focus_prev(group);
    } else if (lv_event_get_key(event) == LV_KEY_LEFT) {
        if (statusbar->side_right) {
            lv_group_focus_prev(group);
        }
    } else if (lv_event_get_key(event) == LV_KEY_RIGHT) {
        if (!statusbar->side_right) {
            lv_group_focus_prev(group);
        }
    }
}

// Where the latest press on a statusbar started, which a gesture is recognised after it moved
static int32_t statusbar_press_x = 0;

static void onStatusbarPressed(lv_event_t* /*event*/) {
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        statusbar_press_x = point.x;
    }
}

static void onStatusbarGesture(lv_event_t* /*event*/) {
    lv_indev_t* indev = lv_indev_active();
    if (indev != nullptr && lv_indev_get_gesture_dir(indev) == LV_DIR_BOTTOM) {
        // The release must not click the pressed widget
        lv_indev_wait_release(indev);
        show_menu(statusbar_press_x);
    }
}

extern "C" {

void lvgl_statusbar_set_menu_callback(void (*callback)(int32_t swipe_x)) {
    recursive_mutex_lock(&statusbar_data().mutex);
    statusbar_data().menu_callback = callback;
    recursive_mutex_unlock(&statusbar_data().mutex);
}

void lvgl_statusbar_set_time_format_24h(bool format_24h) {
    recursive_mutex_lock(&statusbar_data().mutex);
    statusbar_data().time_format_24h = format_24h;
    recursive_mutex_unlock(&statusbar_data().mutex);
    notify_instances();
}

lv_obj_t* lvgl_statusbar_create(lv_obj_t* parent) {
    statusbar_class.height_def = lvgl_statusbar_get_height();
    lv_obj_t* obj = lv_obj_class_create_obj(&statusbar_class, parent);
    lv_obj_class_init_obj(obj);

    auto* statusbar = reinterpret_cast<Statusbar*>(obj);

    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_pad_ver(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(obj, 2, LV_STATE_DEFAULT);
    lv_obj_center(obj);
    auto icon_size = lvgl_get_statusbar_icon_font_height();
    auto icon_padding = lvgl_theme_lerp(2, static_cast<uint32_t>(icon_size * 0.2f), lvgl_theme_get_spacing());

    lv_display_t* display = lv_obj_get_display(obj);
    DisplayShape shape;
    lvgl_display_get_shape(display, &shape);

    // The parent of the icons, which opens the system menu
    lv_obj_t* icon_row;
    if (shape.shape == DISPLAY_SHAPE_CIRCLE) {
        // Two centered rows, as the top of a circle is narrow: the time, and the icons below it
        const int32_t diameter = std::min(lv_display_get_horizontal_resolution(display), lv_display_get_vertical_resolution(display));
        lv_obj_set_height(obj, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_top(obj, diameter / 24, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        statusbar->time = lv_label_create(obj);
        update_time(statusbar);

        icon_row = lvgl_focus_container_create(obj);
        lv_obj_set_style_pad_ver(icon_row, static_cast<int32_t>(icon_size / 10), LV_STATE_DEFAULT);
        lv_obj_set_style_pad_column(icon_row, icon_padding, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(icon_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    } else {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(obj, icon_padding, LV_STATE_DEFAULT);

        if (shape.shape == DISPLAY_SHAPE_ROUNDED) {
            // Moves the content inward from the rounded corners, measured at the top of the icons
            const int32_t content_top = (lvgl_statusbar_get_height() - static_cast<int32_t>(icon_size)) / 2;
            lv_obj_set_style_pad_hor(obj, 2 + lvgl_display_get_row_inset(display, content_top), LV_STATE_DEFAULT);
        }

        statusbar->time = lv_label_create(obj);
        lv_obj_set_style_margin_left(statusbar->time, 4, LV_STATE_DEFAULT);
        update_time(statusbar);

        auto* left_spacer = lv_obj_create(obj);
        lv_obj_set_scrollable(left_spacer, false);
        lv_obj_set_clickable(left_spacer, false);
        lv_obj_set_size(left_spacer, 1, 1);
        lv_obj_set_flex_grow(left_spacer, 1);
        lv_obj_set_style_bg_opa(left_spacer, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(left_spacer, LV_OPA_0, LV_STATE_DEFAULT);
        statusbar->spacer = left_spacer;

        icon_row = lvgl_focus_container_create(obj);
        lv_obj_set_style_pad_column(icon_row, icon_padding, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_row(icon_row, icon_padding, LV_STATE_DEFAULT);
        lv_obj_set_style_margin_right(icon_row, 2, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(icon_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(icon_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        auto* action_container = lv_obj_create(obj);
        lv_obj_remove_style_all(action_container);
        lv_obj_set_size(action_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(action_container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(action_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_scrollable(action_container, false);
        lv_obj_set_hidden(action_container, true);
        statusbar->action_container = action_container;
        statusbar->split_pad_hor = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
    }
    statusbar->icon_row = icon_row;

    lv_obj_set_clickable(icon_row, true);
    lv_obj_add_event_cb(icon_row, onIconRowClicked, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(icon_row, onIconRowKey, LV_EVENT_KEY, statusbar);

    // Swiping down from anywhere on the statusbar opens the system menu
    lv_obj_set_gesture_bubble(obj, false);
    lv_obj_add_event_cb(obj, onStatusbarPressed, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, onStatusbarGesture, LV_EVENT_GESTURE, nullptr);
    if (statusbar->action_container != nullptr) {
        lv_obj_add_event_cb(obj, onSideSizeChanged, LV_EVENT_SIZE_CHANGED, statusbar);
        lv_obj_add_event_cb(statusbar->action_container, onSideSizeChanged, LV_EVENT_SIZE_CHANGED, statusbar);
    }


    recursive_mutex_lock(&statusbar_data().mutex);
    for (int i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
        auto* image = lv_image_create(icon_row);
        lv_obj_set_size(image, icon_size, icon_size); // regular padding doesn't work
        lv_obj_set_style_text_font(image, lvgl_get_statusbar_icon_font(), LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(image, 0, LV_STATE_DEFAULT);
        statusbar->icons[i] = image;

        update_icon(image, &(statusbar_data().icons[i]));
    }
    // Only now is statusbar->icons[] fully populated - see statusbar_constructor()'s comment for
    // why the instance can't be registered any earlier. Registering before unlocking (rather
    // than after) closes a narrow window where a concurrent icon change could be missed by
    // this instance after the icons are populated but before it's registered.
    mutex_lock(&statusbar_data().instances_mutex);
    statusbar->next = statusbar_data().instances;
    statusbar_data().instances = statusbar;
    mutex_unlock(&statusbar_data().instances_mutex);
    recursive_mutex_unlock(&statusbar_data().mutex);

    // Only the icon row is focused, it joins the group by itself
    if (lv_obj_get_group(obj) != nullptr) {
        lv_group_remove_obj(obj);
    }

    return obj;
}

}

static void update_time(Statusbar* statusbar) {
    if (statusbar_data().time_set) {
        bool format24 = statusbar_data().time_format_24h;
        int hours = format24 ? statusbar_data().time_hours : statusbar_data().time_hours % 12;
        lv_label_set_text_fmt(statusbar->time, "%d:%02d", hours, statusbar_data().time_minutes);
    } else {
        lv_label_set_text(statusbar->time, "");
    }
}

// Hides the icons that don't fit, which are the ones with the lowest priority, and the newest ones of those.
// The icons keep their order. Call while holding statusbar_data().mutex, after update_icon() set the icons' visibility.
static void hide_icons_beyond(Statusbar* statusbar, int fitting) {
    // The shown icons, the ones that stay first
    int shown[STATUSBAR_ICON_LIMIT];
    int shown_count = 0;
    for (int i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
        if (!lv_obj_is_hidden(statusbar->icons[i])) {
            shown[shown_count++] = i;
        }
    }
    std::sort(shown, shown + shown_count, [](int a, int b) {
        const StatusbarIcon& first = statusbar_data().icons[a];
        const StatusbarIcon& second = statusbar_data().icons[b];
        if (first.priority != second.priority) {
            return first.priority > second.priority;
        }
        return first.added_order < second.added_order;
    });
    for (int i = std::max(fitting, 0); i < shown_count; ++i) {
        lv_obj_set_hidden(statusbar->icons[shown[i]], true);
    }
}

static int32_t count_fitting_icons(Statusbar* statusbar, int32_t available) {
    const int32_t icon_size = static_cast<int32_t>(lvgl_get_statusbar_icon_font_height());
    const int32_t icon_gap = lv_obj_get_style_pad_row(statusbar->icon_row, LV_PART_MAIN);
    return available >= icon_size ? (available + icon_gap) / (icon_size + icon_gap) : 0;
}

// In the side strip, the time and the action buttons always show, as they are more important than the icons.
// The icons get the height that's left.
static void apply_side_icon_limit(Statusbar* statusbar) {
    lv_obj_t* obj = &statusbar->obj;
    const int32_t gap = lv_obj_get_style_pad_row(obj, LV_PART_MAIN);
    // The time, the icons, the spacer and the actions are separated by 3 gaps, and the spacer is at least 1 pixel high
    const int32_t taken = lv_obj_get_height(statusbar->time) + lv_obj_get_style_margin_bottom(statusbar->time, LV_PART_MAIN) +
        lv_obj_get_height(statusbar->action_container) + 3 * gap + 1;
    hide_icons_beyond(statusbar, static_cast<int>(count_fitting_icons(statusbar, lv_obj_get_content_height(obj) - taken)));
}

// In the top bar, the icons get the width that the time leaves
static void apply_split_icon_limit(Statusbar* statusbar) {
    lv_obj_t* obj = &statusbar->obj;
    const int32_t gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);
    // The time, the spacer and the icons are separated by 2 gaps, and the spacer is at least 1 pixel wide
    const int32_t taken = lv_obj_get_width(statusbar->time) + lv_obj_get_style_margin_left(statusbar->time, LV_PART_MAIN) + 2 * gap + 1 +
        lv_obj_get_style_margin_right(statusbar->icon_row, LV_PART_MAIN) +
        lv_obj_get_style_pad_left(statusbar->icon_row, LV_PART_MAIN) + lv_obj_get_style_pad_right(statusbar->icon_row, LV_PART_MAIN);
    // The icons are in a row here, so the gap between them is the column gap
    const int32_t icon_size = static_cast<int32_t>(lvgl_get_statusbar_icon_font_height());
    const int32_t icon_gap = lv_obj_get_style_pad_column(statusbar->icon_row, LV_PART_MAIN);
    const int32_t available = lv_obj_get_content_width(obj) - taken;
    hide_icons_beyond(statusbar, available >= icon_size ? static_cast<int>((available + icon_gap) / (icon_size + icon_gap)) : 0);
}

static void update_icons(Statusbar* statusbar) {
    if (recursive_mutex_try_lock(&statusbar_data().mutex, 200 / portTICK_PERIOD_MS)) {
        for (int i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
            update_icon(statusbar->icons[i], &(statusbar_data().icons[i]));
        }
        if (statusbar->side) {
            apply_side_icon_limit(statusbar);
        } else if (statusbar->action_container != nullptr) {
            // Circular displays have no action container and show all icons
            apply_split_icon_limit(statusbar);
        }
        recursive_mutex_unlock(&statusbar_data().mutex);
    }
}

// The room for the icons changes with the size of the bar and the actions that apps add
static void onSideSizeChanged(lv_event_t* event) {
    update_icons(static_cast<Statusbar*>(lv_event_get_user_data(event)));
}

static void update_main(Statusbar* statusbar) {
    update_time(statusbar);
    // The icon limit reads the size of the time, which only changes in a layout pass
    lv_obj_update_layout(statusbar->time);
    update_icons(statusbar);
}

static void statusbar_event(const lv_obj_class_t* class_p, lv_event_t* event) {
    // Call the ancestor's event handler
    lv_result_t result = lv_obj_event_base(&statusbar_class, event);
    if (result != LV_RES_OK) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(event);
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(event));

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_invalidate(obj);
    }
}

static void copy_image(char* target, const char* image) {
    if (image == nullptr) {
        target[0] = '\0';
        return;
    }
    if (strlen(image) > LVGL_STATUSBAR_IMAGE_MAX_LENGTH) {
        LOG_W(TAG, "Image too long: %s", image);
    }
    strncpy(target, image, LVGL_STATUSBAR_IMAGE_MAX_LENGTH);
    target[LVGL_STATUSBAR_IMAGE_MAX_LENGTH] = '\0';
}

extern "C" {

int8_t lvgl_statusbar_icon_add(const char* image, bool visible) {
    return lvgl_statusbar_icon_add_with_priority(image, visible, LVGL_STATUSBAR_ICON_PRIORITY_REGULAR);
}

int8_t lvgl_statusbar_icon_add_with_priority(const char* image, bool visible, LvglStatusbarIconPriority priority) {
    recursive_mutex_lock(&statusbar_data().mutex);
    int8_t result = -1;
    for (int8_t i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
        if (!statusbar_data().icons[i].claimed) {
            statusbar_data().icons[i].claimed = true;
            statusbar_data().icons[i].visible = visible;
            copy_image(statusbar_data().icons[i].image, image);
            statusbar_data().icons[i].added_order = statusbar_data().next_added_order++;
            statusbar_data().icons[i].priority = priority;
            result = i;
            LOG_D(TAG, "id %d: added", (int)i);
            break;
        }
    }
    recursive_mutex_unlock(&statusbar_data().mutex);
    if (result < 0) {
        LOG_W(TAG, "No icon slot left, the icon isn't shown");
    }
    notify_instances();
    return result;
}

void lvgl_statusbar_icon_remove(int8_t id) {
    LOG_D(TAG, "id %d: remove", (int)id);
    if (id == -1) {
        return;
    }
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    recursive_mutex_lock(&statusbar_data().mutex);
    StatusbarIcon* icon = &statusbar_data().icons[id];
    icon->claimed = false;
    icon->visible = false;
    icon->image[0] = '\0';
    recursive_mutex_unlock(&statusbar_data().mutex);
    notify_instances();
}

void lvgl_statusbar_icon_set_image(int8_t id, const char* image) {
    LOG_D(TAG, "id %d: set image %s", (int)id, image != nullptr ? image : "(none)");
    if (id == -1) {
        return;
    }
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    recursive_mutex_lock(&statusbar_data().mutex);
    StatusbarIcon* icon = &statusbar_data().icons[id];
    check(icon->claimed);
    copy_image(icon->image, image);
    recursive_mutex_unlock(&statusbar_data().mutex);
    notify_instances();
}

void lvgl_statusbar_icon_set_visibility(int8_t id, bool visible) {
    LOG_D(TAG, "id %d: set visibility %d", (int)id, (int)visible);
    if (id == -1) {
        return;
    }
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    recursive_mutex_lock(&statusbar_data().mutex);
    StatusbarIcon* icon = &statusbar_data().icons[id];
    check(icon->claimed);
    icon->visible = visible;
    recursive_mutex_unlock(&statusbar_data().mutex);
    notify_instances();
}

}

// The colour that is visible behind the object: its own background or the first visible one of its parents
static lv_color_t get_visible_bg_color(lv_obj_t* obj) {
    for (lv_obj_t* current = obj; current != nullptr; current = lv_obj_get_parent(current)) {
        if (lv_obj_get_style_bg_opa(current, LV_PART_MAIN) >= LV_OPA_50) {
            return lv_obj_get_style_bg_color(current, LV_PART_MAIN);
        }
    }
    return lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
}

// The width of an app's action button in the side strip, measured with a temporary button like the ones apps add
static int32_t get_action_button_width(lv_obj_t* actionContainer) {
    auto* button = lvgl_icon_button_create(actionContainer);
    // It must not take the focus
    if (lv_obj_get_group(button) != nullptr) {
        lv_group_remove_obj(button);
    }
    auto* label = lv_label_create(button);
    lv_obj_set_style_text_font(label, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_label_set_text(label, LVGL_ICON_SHARED_SETTINGS);
    lv_obj_update_layout(button);
    const int32_t width = lv_obj_get_width(button);
    lv_obj_delete(button);
    return width;
}

// The width of the widest time that the label can show, as the font's digits can differ in width
static int32_t get_widest_time_width(const lv_font_t* font, int32_t letterSpace) {
    char widest_digit = '0';
    int32_t widest_digit_width = 0;
    for (char digit = '0'; digit <= '9'; digit++) {
        const char text[2] = { digit, '\0' };
        lv_point_t size;
        lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x > widest_digit_width) {
            widest_digit_width = size.x;
            widest_digit = digit;
        }
    }
    const char text[6] = { widest_digit, widest_digit, ':', widest_digit, widest_digit, '\0' };
    lv_point_t size;
    lv_text_get_size(&size, text, font, letterSpace, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}

// How far the selection ring of the icon row reaches outside of it
static int32_t get_focus_ring_extent(lv_obj_t* icon_row) {
    // The row can be focused already, which must stay so
    const bool wasFocused = lv_obj_has_state(icon_row, LV_STATE_FOCUS_KEY);
    lv_obj_add_state(icon_row, LV_STATE_FOCUS_KEY);
    const int32_t extent = lv_obj_get_style_outline_width(icon_row, LV_PART_MAIN) + std::max<int32_t>(0, lv_obj_get_style_outline_pad(icon_row, LV_PART_MAIN));
    if (!wasFocused) {
        lv_obj_remove_state(icon_row, LV_STATE_FOCUS_KEY);
    }
    return extent;
}

extern "C" {

void lvgl_statusbar_set_side_right(lv_obj_t* obj, bool right) {
    reinterpret_cast<Statusbar*>(obj)->side_right = right;
}

void lvgl_statusbar_set_side_insets(lv_obj_t* obj, int32_t top, int32_t bottom) {
    auto* statusbar = reinterpret_cast<Statusbar*>(obj);
    if (statusbar->side_inset_top == top && statusbar->side_inset_bottom == bottom) {
        return;
    }
    if (statusbar->side) {
        const uint8_t spacing = lvgl_theme_get_spacing();
        lv_obj_set_style_pad_top(obj, lvgl_theme_lerp(0, 4, spacing) + top, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(obj, lvgl_theme_lerp(0, 8, spacing) + bottom, LV_STATE_DEFAULT);
        // The strip keeps its size, so no size event follows to recalculate how many icons fit
        update_icons(statusbar);
    }
    statusbar->side_inset_top = top;
    statusbar->side_inset_bottom = bottom;
}

void lvgl_statusbar_set_side(lv_obj_t* obj, bool side) {
    auto* statusbar = reinterpret_cast<Statusbar*>(obj);
    // Circular displays only have room for the statusbar at the top
    if (statusbar->action_container == nullptr || (statusbar->layout_applied && statusbar->side == side)) {
        return;
    }
    statusbar->layout_applied = true;
    statusbar->side = side;

    lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_COLOR, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(obj, LV_STYLE_BG_OPA, LV_PART_MAIN);
    if (side) {
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_hor(obj, 2, LV_STATE_DEFAULT);
        // A fixed width, so the strip doesn't change when an app adds or removes action buttons
        lv_obj_set_hidden(statusbar->action_container, false);
        const int32_t time_width = get_widest_time_width(lvgl_get_text_font(FONT_SIZE_SMALL), lv_obj_get_style_text_letter_space(statusbar->time, LV_PART_MAIN));
        const int32_t content_width = std::max(get_action_button_width(statusbar->action_container), time_width);
        lv_obj_set_size(obj, content_width + 2 * 2, LV_PCT(100));
        // As wide as the strip allows, with room left for the selection ring
        lv_obj_set_width(statusbar->icon_row, std::max<int32_t>(content_width + 2 * 2 - 2 * get_focus_ring_extent(statusbar->icon_row), 1));
        lv_obj_set_height(statusbar->icon_row, LV_SIZE_CONTENT);
        // Keeps the action buttons away from the screen's bottom edge. The compact layout has no room to spare.
        const uint8_t spacing = lvgl_theme_get_spacing();
        lv_obj_set_style_pad_top(obj, lvgl_theme_lerp(0, 4, spacing) + statusbar->side_inset_top, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(obj, lvgl_theme_lerp(0, 8, spacing) + statusbar->side_inset_bottom, LV_STATE_DEFAULT);
        // Slightly darker than the colour behind it, so the strip stands out from the app next to it
        const lv_color_t background = get_visible_bg_color(obj);
        lv_obj_set_style_bg_color(obj, lv_color_darken(background, LV_OPA_20), LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
        lv_obj_set_style_margin_left(statusbar->time, 0, LV_STATE_DEFAULT);
        // The small font fits the time in the narrow strip
        lv_obj_set_style_text_font(statusbar->time, lvgl_get_text_font(FONT_SIZE_SMALL), LV_STATE_DEFAULT);
        lv_obj_set_style_margin_bottom(statusbar->time, 10, LV_STATE_DEFAULT);
        // As wide as the strip, so the column of items is centered on the strip's full width
        lv_obj_set_width(statusbar->time, LV_PCT(100));
        lv_obj_set_style_text_align(statusbar->time, LV_TEXT_ALIGN_CENTER, LV_STATE_DEFAULT);
        // The time stays on one line, also when it's wider than expected
        lv_label_set_long_mode(statusbar->time, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_margin_right(statusbar->icon_row, 0, LV_STATE_DEFAULT);
        // The icon that's last in the top bar is closest to the screen's corner there, so it goes on top
        lv_obj_set_flex_flow(statusbar->icon_row, LV_FLEX_FLOW_COLUMN_REVERSE);
        // The time, the icons, the spacer and then the actions
        lv_obj_move_to_index(statusbar->icon_row, 1);
        lv_obj_set_hidden(statusbar->action_container, false);
    } else {
        lv_obj_set_size(obj, LV_PCT(100), lvgl_statusbar_get_height());
        // As high as the bar allows, with room left for the selection ring
        lv_obj_set_width(statusbar->icon_row, LV_SIZE_CONTENT);
        lv_obj_set_height(statusbar->icon_row, std::max<int32_t>(lvgl_statusbar_get_height() - 2 * get_focus_ring_extent(statusbar->icon_row), 1));
        lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_hor(obj, statusbar->split_pad_hor, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_ver(obj, 0, LV_STATE_DEFAULT);
        lv_obj_set_style_margin_left(statusbar->time, 4, LV_STATE_DEFAULT);
        lv_obj_remove_local_style_prop(statusbar->time, LV_STYLE_TEXT_FONT, LV_PART_MAIN);
        lv_obj_remove_local_style_prop(statusbar->time, LV_STYLE_MARGIN_BOTTOM, LV_PART_MAIN);
        lv_obj_set_width(statusbar->time, LV_SIZE_CONTENT);
        lv_obj_remove_local_style_prop(statusbar->time, LV_STYLE_TEXT_ALIGN, LV_PART_MAIN);
        lv_label_set_long_mode(statusbar->time, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_margin_right(statusbar->icon_row, 2, LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(statusbar->icon_row, LV_FLEX_FLOW_ROW);
        // The time, the spacer and then the icons
        lv_obj_move_to_index(statusbar->icon_row, 2);
        lv_obj_set_hidden(statusbar->action_container, true);
    }
    // Both layouts only show the icons that fit
    update_icons(statusbar);
}

lv_obj_t* lvgl_statusbar_get_action_container(lv_obj_t* obj) {
    return reinterpret_cast<Statusbar*>(obj)->action_container;
}

int32_t lvgl_statusbar_get_height() {
    const auto icon_size = lvgl_get_statusbar_icon_font_height();
    const auto vertical_padding = static_cast<uint32_t>((static_cast<float>(icon_size) * 0.1f));
    return static_cast<int32_t>(icon_size + (2 * vertical_padding) + 4);
}

}
