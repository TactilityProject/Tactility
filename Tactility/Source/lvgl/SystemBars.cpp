#include <Tactility/lvgl/SystemBars.h>

#include <Tactility/lvgl/Statusbar.h>
#include <Tactility/lvgl/SystemMenu.h>

#include <lvgl_window_manager/window_manager.h>

#include <lvgl/theme.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace tt::lvgl {

using settings::launcher::SystemBarsLayout;

namespace {

struct Listener {
    SystemBarsListenerId id;
    std::function<void()> callback;
};

// All state is guarded by the LVGL lock
struct SystemBarsState {
    lv_obj_t* container = nullptr;
    lv_obj_t* statusbar = nullptr;
    lv_obj_t* appContainer = nullptr;
    lv_display_t* display = nullptr;
    SystemBarsLayout layout = SystemBarsLayout::Split;
    bool applied = false;
    // Where the current press of a pointer started, for swipes from the top edge
    lv_point_t pressPoint = { 0, 0 };
    SystemBarsListenerId nextListenerId = 1;
    std::vector<Listener> listeners;
};

SystemBarsState state;

void onResolutionChanged(lv_event_t* /*event*/) {
    systemBarsRefresh();
}

// The target of a pointer device's own events is the device
lv_indev_t* getEventIndev(lv_event_t* event) {
    return static_cast<lv_indev_t*>(lv_event_get_current_target(event));
}

void onPointerPressed(lv_event_t* event) {
    lv_indev_get_point(getEventIndev(event), &state.pressPoint);
}

// In the side layout, the top edge has no statusbar to swipe down from, so a swipe down that starts there opens the system menu
void onPointerReleased(lv_event_t* event) {
    if (state.layout != SystemBarsLayout::Side || window_manager_get_active_layer() != WINDOW_LAYER_APP) {
        return;
    }
    lv_indev_t* indev = getEventIndev(event);
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const int32_t edge = statusbar_get_height();
    const int32_t dy = point.y - state.pressPoint.y;
    const int32_t dx = point.x - state.pressPoint.x;
    if (state.pressPoint.y <= edge && dy >= edge * 2 && dy > 2 * LV_ABS(dx)) {
        // The release must not click the widget below the pointer
        lv_indev_reset(indev, nullptr);
        systemMenuShow();
    }
}

void forEachPointer(void (*action)(lv_indev_t* indev)) {
    for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev)) {
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            action(indev);
        }
    }
}

void removePointerCallbacks(lv_indev_t* indev) {
    lv_indev_remove_event_cb_with_user_data(indev, onPointerPressed, nullptr);
    lv_indev_remove_event_cb_with_user_data(indev, onPointerReleased, nullptr);
}

void addPointerCallbacks(lv_indev_t* indev) {
    removePointerCallbacks(indev);
    lv_indev_add_event_cb(indev, onPointerPressed, LV_EVENT_PRESSED, nullptr);
    lv_indev_add_event_cb(indev, onPointerReleased, LV_EVENT_RELEASED, nullptr);
}

// The monochrome theme has no tints, so a line separates the strip from the app
void applySeparator(bool side) {
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_SIDE, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_COLOR, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_OPA, LV_PART_MAIN);
    if (side && lvgl_theme_is_mono()) {
        lv_obj_set_style_border_side(state.statusbar, LV_BORDER_SIDE_RIGHT, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(state.statusbar, 1, LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(state.statusbar, lv_obj_get_style_text_color(state.statusbar, LV_PART_MAIN), LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(state.statusbar, LV_OPA_COVER, LV_STATE_DEFAULT);
    }
}

// The statusbar is the container's first child: on top in the split layout, and on the left in the side layout
void applyLayout(SystemBarsLayout layout) {
    const bool side = layout == SystemBarsLayout::Side;
    statusbar_set_side(state.statusbar, side);
    applySeparator(side);
    if (side) {
        lv_obj_set_flex_flow(state.container, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(state.appContainer, LV_SIZE_CONTENT, LV_PCT(100));
    } else {
        lv_obj_set_flex_flow(state.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(state.appContainer, LV_PCT(100), LV_SIZE_CONTENT);
    }
    lv_obj_set_flex_grow(state.appContainer, 1);
}

}

void systemBarsAttach(lv_obj_t* container, lv_obj_t* statusbar, lv_obj_t* appContainer) {
    systemBarsDetach();
    state.container = container;
    state.statusbar = statusbar;
    state.appContainer = appContainer;
    state.display = lv_obj_get_display(container);
    state.applied = false;
    lv_display_add_event_cb(state.display, onResolutionChanged, LV_EVENT_RESOLUTION_CHANGED, nullptr);
    systemBarsRefresh();
}

void systemBarsDetach() {
    if (state.display != nullptr) {
        lv_display_remove_event_cb_with_user_data(state.display, onResolutionChanged, nullptr);
        forEachPointer(removePointerCallbacks);
    }
    state.container = nullptr;
    state.statusbar = nullptr;
    state.appContainer = nullptr;
    state.display = nullptr;
    state.layout = SystemBarsLayout::Split;
}

void systemBarsRefresh() {
    if (state.container == nullptr) {
        return;
    }
    // Also picks up pointer devices that were added since the last refresh
    forEachPointer(addPointerCallbacks);
    const auto settings = settings::launcher::loadOrGetDefault();
    const auto width = lv_display_get_horizontal_resolution(state.display);
    const auto height = lv_display_get_vertical_resolution(state.display);
    auto layout = settings::launcher::resolveSystemBarsLayout(settings, settings::launcher::getSystemBarsCapabilities(), width, height);
    // Circular displays only have room for the statusbar at the top
    if (statusbar_get_action_container(state.statusbar) == nullptr) {
        layout = SystemBarsLayout::Split;
    }
    if (state.applied && layout == state.layout) {
        return;
    }
    applyLayout(layout);
    state.layout = layout;
    state.applied = true;

    // A copy, as a listener may remove itself
    const auto listeners = state.listeners;
    for (const auto& listener : listeners) {
        listener.callback();
    }
}

SystemBarsLayout systemBarsGetLayout() {
    return state.layout;
}

lv_obj_t* systemBarsGetActionContainer() {
    if (state.statusbar == nullptr || state.layout != SystemBarsLayout::Side) {
        return nullptr;
    }
    return statusbar_get_action_container(state.statusbar);
}

SystemBarsListenerId systemBarsAddListener(std::function<void()> listener) {
    const auto id = state.nextListenerId++;
    state.listeners.push_back({ id, std::move(listener) });
    return id;
}

void systemBarsRemoveListener(SystemBarsListenerId id) {
    std::erase_if(state.listeners, [id](const Listener& listener) { return listener.id == id; });
}

}
