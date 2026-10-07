#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <Tactility/PubSub.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Timer.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/settings/Time.h>

#include <tactility/check.h>
#include <tactility/log.h>
#include <tactility/system_event.h>
#include <tactility/time.h>

#include <app/start.h>

#include <lvgl/devices/indev.h>
#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/insets.h>
#include <lvgl/lvgl.h>
#include <lvgl/theme.h>

#include <algorithm>
#include <memory>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

namespace tt::lvgl {

#if defined(CONFIG_TT_LVGL_STATUSBAR_COLORS_INVERTED) && CONFIG_TT_LVGL_STATUSBAR_COLORS_INVERTED
constexpr bool STATUSBAR_COLORS_INVERTED = true;
#else
constexpr bool STATUSBAR_COLORS_INVERTED = false;
#endif

constexpr auto* TAG = "statusbar";

static void onUpdateTime();

struct StatusbarIcon {
    std::string image;
    std::string appId;
    bool visible = false;
    bool claimed = false;
};

struct StatusbarData {
    RecursiveMutex mutex;
    std::shared_ptr<PubSub<void*>> pubsub = std::make_shared<PubSub<void*>>();
    StatusbarIcon icons[STATUSBAR_ICON_LIMIT] = {};
    Timer* time_update_timer = new Timer(Timer::Type::Once, 200 / portTICK_PERIOD_MS, [] { onUpdateTime(); });
    uint8_t time_hours = 0;
    uint8_t time_minutes = 0;
    bool time_set = false;
};

static StatusbarData statusbar_data;

typedef struct {
    lv_obj_t obj;
    lv_obj_t* time;
    lv_obj_t* icons[STATUSBAR_ICON_LIMIT];
    lv_obj_t* battery_icon;
    PubSub<void*>::SubscriptionHandle pubsub_subscription;
} Statusbar;

static void statusbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void statusbar_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj);
static void statusbar_event(const lv_obj_class_t* class_p, lv_event_t* event);

static void update_time(Statusbar* statusbar);
static void update_main(Statusbar* statusbar);

static TickType_t getNextUpdateTime() {
    time_t now = ::time(nullptr);
    tm* tm_struct = localtime(&now);
    uint32_t seconds_to_wait = 60U - tm_struct->tm_sec;
    LOG_D(TAG, "Update in %d s", (int)seconds_to_wait);
    return pdMS_TO_TICKS(seconds_to_wait * 1000U);
}

static void onUpdateTime() {
    time_t now = ::time(nullptr);
    tm* tm_struct = localtime(&now);

    if (statusbar_data.mutex.lock(100 / portTICK_PERIOD_MS)) {
        if (tm_struct->tm_year >= (2025 - 1900)) {
            statusbar_data.time_hours = tm_struct->tm_hour;
            statusbar_data.time_minutes = tm_struct->tm_min;
            statusbar_data.time_set = true;

            // Reschedule
            statusbar_data.time_update_timer->reset(getNextUpdateTime());

            // Notify widget
            statusbar_data.pubsub->publish(nullptr);
        } else {
            statusbar_data.time_update_timer->reset(pdMS_TO_TICKS(60000U));
        }

        statusbar_data.mutex.unlock();
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

static void statusbar_pubsub_event(Statusbar* statusbar) {
    LOG_D(TAG, "Update event");
    if (lvgl_try_lock(500 / portTICK_PERIOD_MS)) {
        update_main(statusbar);
        lv_obj_invalidate(&statusbar->obj);
        lvgl_unlock();
    } else {
        LOG_W(TAG, "Mutex acquisition timeout (%s)", "Statusbar");
    }
}

static void onTimeChanged(struct SystemEvent* /*event*/, void* /*context*/) {
    if (statusbar_data.mutex.lock()) {
        statusbar_data.time_update_timer->reset(5);
        statusbar_data.mutex.unlock();
    }
}

static void statusbar_constructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    LV_UNUSED(class_p);
    LV_TRACE_OBJ_CREATE("begin");
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    LV_TRACE_OBJ_CREATE("finished");

    // Deliberately does NOT subscribe to statusbar_data.pubsub here - that happens at the end of
    // statusbar_create(), once statusbar->icons[] is actually populated. Subscribing this early
    // would let a concurrent statusbar_icon_add()/_remove()/_set_image()/_set_visibility() call
    // from another task publish and run update_icon() on a still-null icons[] slot before this
    // instance's own creation loop below has had a chance to fill it in.
    if (!statusbar_data.time_update_timer->isRunning()) {
        statusbar_data.time_update_timer->start();
        system_event_callback_add(KERNEL_EVENT_TIME_CHANGED, onTimeChanged, nullptr);
    }
}

static void statusbar_destructor(const lv_obj_class_t* class_p, lv_obj_t* obj) {
    auto* statusbar = (Statusbar*)obj;
    statusbar_data.pubsub->unsubscribe(statusbar->pubsub_subscription);
}

static void update_icon(lv_obj_t* image, const StatusbarIcon* icon) {
    if (!icon->image.empty() && icon->visible && icon->claimed) {
        lv_image_set_src(image, icon->image.c_str());
        lv_obj_remove_flag(image, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(image, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_flag(image, LV_OBJ_FLAG_CLICKABLE, !icon->appId.empty());
}

static void onIconClicked(lv_event_t* event) {
    const auto index = reinterpret_cast<intptr_t>(lv_event_get_user_data(event));
    statusbar_data.mutex.lock();
    const std::string app_id = statusbar_data.icons[index].appId;
    statusbar_data.mutex.unlock();
    if (app_id.empty()) {
        return;
    }
    AppStartContext context;
    uint32_t instance_id = 0;
    if (app_start_context_from_id(app_id.c_str(), &context) == ERROR_NONE) {
        app_start_with_context(&context, &instance_id);
    }
}

lv_obj_t* statusbar_create(lv_obj_t* parent) {
    statusbar_class.height_def = statusbar_get_height();
    lv_obj_t* obj = lv_obj_class_create_obj(&statusbar_class, parent);
    lv_obj_class_init_obj(obj);

    auto* statusbar = reinterpret_cast<Statusbar*>(obj);

    lv_obj_set_width(obj, LV_PCT(100));
    lv_obj_set_style_pad_ver(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_hor(obj, 2, LV_STATE_DEFAULT);
    lv_obj_center(obj);
    auto icon_size = lvgl_get_statusbar_icon_font_height();
    auto ui_density = lvgl_get_ui_density();
    auto icon_padding = (ui_density != LVGL_UI_DENSITY_COMPACT) ? static_cast<uint32_t>(icon_size * 0.2f) : 2;

    lv_display_t* display = lv_obj_get_display(obj);
    DisplayShape shape;
    lvgl_display_get_shape(display, &shape);

    // The parent of the icons, which grid navigation moves through
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

        icon_row = lv_obj_create(obj);
        lv_obj_remove_style_all(icon_row);
        lv_obj_set_size(icon_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
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
            const int32_t content_top = (statusbar_get_height() - static_cast<int32_t>(icon_size)) / 2;
            lv_obj_set_style_pad_hor(obj, 2 + lvgl_display_get_row_inset(display, content_top), LV_STATE_DEFAULT);
        }

        statusbar->time = lv_label_create(obj);
        lv_obj_set_style_margin_left(statusbar->time, 4, LV_STATE_DEFAULT);
        update_time(statusbar);

        auto* left_spacer = lv_obj_create(obj);
        lv_obj_remove_flag(left_spacer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(left_spacer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(left_spacer, 1, 1);
        lv_obj_set_flex_grow(left_spacer, 1);
        lv_obj_set_style_bg_opa(left_spacer, LV_OPA_0, LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(left_spacer, LV_OPA_0, LV_STATE_DEFAULT);

        icon_row = obj;
    }

    // The monochrome theme's focus ring doesn't contrast with the statusbar, so the icons get their own
    const bool own_focus_ring = lvgl_theme_is_mono();
    const int32_t focus_width = (ui_density != LVGL_UI_DENSITY_COMPACT) ? 2 : 1;
    const lv_color_t focus_color = lv_obj_get_style_text_color(obj, LV_PART_MAIN);

    statusbar_data.mutex.lock(MAX_TICKS);
    for (int i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
        auto* image = lv_image_create(icon_row);
        lv_obj_set_size(image, icon_size, icon_size); // regular padding doesn't work
        lv_obj_set_style_text_font(image, lvgl_get_statusbar_icon_font(), LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(image, 0, LV_STATE_DEFAULT);
        if (own_focus_ring) {
            lv_obj_set_style_outline_color(image, focus_color, LV_STATE_FOCUS_KEY);
            lv_obj_set_style_outline_width(image, focus_width, LV_STATE_FOCUS_KEY);
            lv_obj_set_style_outline_pad(image, -focus_width, LV_STATE_FOCUS_KEY);
        }
        lv_obj_add_event_cb(image, onIconClicked, LV_EVENT_SHORT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(i)));
        statusbar->icons[i] = image;

        update_icon(image, &(statusbar_data.icons[i]));
    }
    // Only now is statusbar->icons[] fully populated - see statusbar_constructor()'s comment for
    // why the subscription can't be registered any earlier. Subscribing before unlocking (rather
    // than after) closes a narrow window where a concurrent statusbar_icon_add()/_remove()/
    // _set_image()/_set_visibility() call could publish - and be missed by this instance - after
    // the icons are populated but before it's subscribed.
    statusbar->pubsub_subscription = statusbar_data.pubsub->subscribe([statusbar](auto) {
        statusbar_pubsub_event(statusbar);
    });
    statusbar_data.mutex.unlock();

    // Without a touchscreen, the icons are selected with the keys
    if (lvgl_indev_exists(LV_INDEV_TYPE_KEYPAD)) {
        lvgl_grid_navigation_add(icon_row);
    }
    // Only the icon row is focused when it isn't the statusbar itself
    if (icon_row != obj && lv_obj_get_group(obj) != nullptr) {
        lv_group_remove_obj(obj);
    }

    return obj;
}

static void update_time(Statusbar* statusbar) {
    if (statusbar_data.time_set) {
        bool format24 = settings::isTimeFormat24Hour();
        int hours = format24 ? statusbar_data.time_hours : statusbar_data.time_hours % 12;
        lv_label_set_text_fmt(statusbar->time, "%d:%02d", hours, statusbar_data.time_minutes);
    } else {
        lv_label_set_text(statusbar->time, "");
    }
}

static void update_main(Statusbar* statusbar) {
    update_time(statusbar);

    if (statusbar_data.mutex.lock(200 / portTICK_PERIOD_MS)) {
        for (int i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
            update_icon(statusbar->icons[i], &(statusbar_data.icons[i]));
        }
        statusbar_data.mutex.unlock();
    }
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

int8_t statusbar_icon_add(const std::string& image, bool visible) {
    statusbar_data.mutex.lock();
    int8_t result = -1;
    for (int8_t i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
        if (!statusbar_data.icons[i].claimed) {
            statusbar_data.icons[i].claimed = true;
            statusbar_data.icons[i].visible = visible;
            statusbar_data.icons[i].image = image;
            result = i;
            LOG_D(TAG, "id %d: added", (int)i);
            break;
        }
    }
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
    return result;
}

int8_t statusbar_icon_add() {
    return statusbar_icon_add("", false);
}

void statusbar_icon_remove(int8_t id) {
    LOG_D(TAG, "id %d: remove", (int)id);
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    statusbar_data.mutex.lock();
    StatusbarIcon* icon = &statusbar_data.icons[id];
    icon->claimed = false;
    icon->visible = false;
    icon->image = "";
    icon->appId = "";
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
}

void statusbar_icon_set_image(int8_t id, const std::string& image) {
    if (image.empty()) {
        LOG_D(TAG, "id %d: set image (none)", (int)id);
    } else {
        LOG_D(TAG, "id %d: set image %s", (int)id, image.c_str());
    }
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    statusbar_data.mutex.lock();
    StatusbarIcon* icon = &statusbar_data.icons[id];
    check(icon->claimed);
    icon->image = image;
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
}

void statusbar_icon_set_visibility(int8_t id, bool visible) {
    LOG_D(TAG, "id %d: set visibility %d", (int)id, (int)visible);
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    statusbar_data.mutex.lock();
    StatusbarIcon* icon = &statusbar_data.icons[id];
    check(icon->claimed);
    icon->visible = visible;
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
}

void statusbar_icon_set_app(int8_t id, const std::string& appId) {
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    statusbar_data.mutex.lock();
    StatusbarIcon* icon = &statusbar_data.icons[id];
    check(icon->claimed);
    icon->appId = appId;
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
}

int statusbar_get_height() {
    const auto icon_size = lvgl_get_statusbar_icon_font_height();
    const auto vertical_padding = static_cast<uint32_t>((static_cast<float>(icon_size) * 0.1f));
    return icon_size + (2 * vertical_padding);
}

} // namespace
