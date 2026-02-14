#define LV_USE_PRIVATE_API 1 // For actual lv_obj_t declaration

#include <Tactility/LogMessages.h>
#include <Tactility/Logger.h>
#include <Tactility/PubSub.h>
#include <Tactility/RecursiveMutex.h>
#include <Tactility/Tactility.h>
#include <Tactility/Timer.h>
#include <Tactility/kernel/SystemEvents.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/lvgl/Style.h>
#include <Tactility/settings/Time.h>

#include <tactility/check.h>
#include <tactility/lvgl_fonts.h>

#include <lvgl.h>

#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#else
#define CONFIG_TT_LVGL_SCALE 100
#endif

namespace tt::lvgl {

static const auto LOGGER = Logger("statusbar");

static void onUpdateTime();

struct StatusbarIcon {
    std::string image;
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
    kernel::SystemEventSubscription systemEventSubscription = 0;
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
    if (LOGGER.isLoggingDebug()) {
        LOGGER.debug("Update in {} s", seconds_to_wait);
    }
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
    if (LOGGER.isLoggingDebug()) {
        LOGGER.debug("Update event");
    }
    if (lock(defaultLockTime)) {
        update_main(statusbar);
        lv_obj_invalidate(&statusbar->obj);
        unlock();
    } else {
        LOGGER.warn(LOG_MESSAGE_MUTEX_LOCK_FAILED_FMT, "Statusbar");
    }
}

static void onTimeChanged(kernel::SystemEvent event) {
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
    auto* statusbar = (Statusbar*)obj;
    statusbar->pubsub_subscription = statusbar_data.pubsub->subscribe([statusbar](auto) {
        statusbar_pubsub_event(statusbar);
    });

    if (!statusbar_data.time_update_timer->isRunning()) {
        statusbar_data.time_update_timer->start();
        statusbar_data.systemEventSubscription = kernel::subscribeSystemEvent(
            kernel::SystemEvent::Time,
            onTimeChanged
        );
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
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    auto icon_size = lvgl_get_statusbar_icon_font_height();
    auto ui_density = hal::getConfiguration()->uiDensity;
    auto icon_padding = (ui_density != hal::UiDensity::Compact) ? static_cast<uint32_t>(icon_size * 0.2f) : 2;
    lv_obj_set_style_pad_column(obj, icon_padding, LV_STATE_DEFAULT);

    statusbar->time = lv_label_create(obj);
    lv_obj_set_style_text_color(statusbar->time, lv_color_white(), LV_STATE_DEFAULT);
    lv_obj_set_style_margin_left(statusbar->time, 4, LV_STATE_DEFAULT);
    update_time(statusbar);

    auto* left_spacer = lv_obj_create(obj);
    lv_obj_set_size(left_spacer, 1, 1);
    obj_set_style_bg_invisible(left_spacer);
    lv_obj_set_flex_grow(left_spacer, 1);

    if (statusbar_data.mutex.lock(kernel::MAX_TICKS)) {
        for (int i = 0; i < STATUSBAR_ICON_LIMIT; ++i) {
            auto* image = lv_image_create(obj);
            lv_obj_set_size(image, icon_size, icon_size); // regular padding doesn't work
            lv_obj_set_style_text_font(image, lvgl_get_statusbar_icon_font(), LV_STATE_DEFAULT);
            lv_obj_set_style_pad_all(image, 0, LV_STATE_DEFAULT);
            obj_set_style_bg_blacken(image);
            statusbar->icons[i] = image;

            update_icon(image, &(statusbar_data.icons[i]));
        }
        statusbar_data.mutex.unlock();
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
            if (LOGGER.isLoggingDebug()) {
                LOGGER.debug("id {}: added", i);
            }
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
    if (LOGGER.isLoggingDebug()) {
        LOGGER.debug("id {}: remove", id);
    }
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    statusbar_data.mutex.lock();
    StatusbarIcon* icon = &statusbar_data.icons[id];
    icon->claimed = false;
    icon->visible = false;
    icon->image = "";
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
}

void statusbar_icon_set_image(int8_t id, const std::string& image) {
    if (LOGGER.isLoggingDebug()) {
        if (image.empty()) {
            LOGGER.debug("id {}: set image (none)", id);
        } else {
            LOGGER.debug("id {}: set image {}", id, image);
        }
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
    if (LOGGER.isLoggingDebug()) {
        LOGGER.debug("id {}: set visibility {}", id, visible);
    }
    check(id >= 0 && id < STATUSBAR_ICON_LIMIT);
    statusbar_data.mutex.lock();
    StatusbarIcon* icon = &statusbar_data.icons[id];
    check(icon->claimed);
    icon->visible = visible;
    statusbar_data.mutex.unlock();
    statusbar_data.pubsub->publish(nullptr);
}

int statusbar_get_height() {
    const auto icon_size = lvgl_get_statusbar_icon_font_height();
    const auto vertical_padding = static_cast<uint32_t>((static_cast<float>(icon_size) * 0.1f));
    return icon_size + (2 * vertical_padding);
}

} // namespace
