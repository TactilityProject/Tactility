// SPDX-License-Identifier: Apache-2.0
#include "system_bars_internal.h"

#include <lvgl_window_manager/system_bars.h>
#include <lvgl_window_manager/window_manager.h>

#include <lvgl/fonts.h>
#include <lvgl/grid_navigation.h>
#include <lvgl/icons/shared.h>
#include <lvgl/insets.h>
#include <lvgl/theme.h>
#include <lvgl/widgets/icon_button.h>
#include <lvgl/widgets/statusbar.h>

#include <algorithm>

namespace {

// All state is guarded by the LVGL lock
struct SystemBarsState {
    SystemBarsConfig config = {};
    lv_obj_t* container = nullptr;
    lv_obj_t* statusbar = nullptr;
    lv_obj_t* appContainer = nullptr;
    lv_obj_t* pageBar = nullptr;
    lv_obj_t* mainButton = nullptr;
    lv_obj_t* mainIcon = nullptr;
    lv_obj_t* previousButton = nullptr;
    lv_obj_t* previousIcon = nullptr;
    lv_obj_t* nextButton = nullptr;
    lv_obj_t* nextIcon = nullptr;
    lv_display_t* display = nullptr;
    SystemBarsLayout layout = SYSTEM_BARS_LAYOUT_SPLIT;
    SystemBarsAlignment alignment = SYSTEM_BARS_ALIGNMENT_LEFT;
    bool applied = false;
    // Where the current press of a pointer started, for swipes from the top edge
    lv_point_t pressPoint = { 0, 0 };
    SystemBarsCallback onMain = nullptr;
    void* mainUserData = nullptr;
    SystemBarsCallback onPrevious = nullptr;
    SystemBarsCallback onNext = nullptr;
    void* pageUserData = nullptr;
};

SystemBarsState state;

bool isInside(lv_obj_t* obj, lv_obj_t* ancestor) {
    for (lv_obj_t* current = obj; current != nullptr; current = lv_obj_get_parent(current)) {
        if (current == ancestor) {
            return true;
        }
    }
    return false;
}

// The page bar is only shown when it has a button to show
void updatePageBarVisibility() {
    if (state.pageBar == nullptr) {
        return;
    }
    bool hasButton = false;
    const uint32_t count = lv_obj_get_child_count(state.pageBar);
    for (uint32_t i = 0; i < count && !hasButton; i++) {
        lv_obj_t* child = lv_obj_get_child(state.pageBar, static_cast<int32_t>(i));
        hasButton = !lv_obj_is_hidden(child) && lv_obj_is_clickable(child);
    }
    lv_obj_set_hidden(state.pageBar, !hasButton);
}

void onMainPressed(lv_event_t* /*event*/) {
    if (state.onMain != nullptr) {
        state.onMain(state.mainUserData);
    }
}

void onPreviousPressed(lv_event_t* /*event*/) {
    if (state.onPrevious != nullptr) {
        state.onPrevious(state.pageUserData);
    }
}

void onNextPressed(lv_event_t* /*event*/) {
    if (state.onNext != nullptr) {
        state.onNext(state.pageUserData);
    }
}

lv_obj_t* findFirstButton(lv_obj_t* bar) {
    const uint32_t count = lv_obj_get_child_count(bar);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t* child = lv_obj_get_child(bar, static_cast<int32_t>(i));
        if (!lv_obj_is_hidden(child) && lv_obj_is_clickable(child)) {
            return child;
        }
    }
    return nullptr;
}

// Keys enter the page bar on the previous page button, or on the first visible button when it's hidden
void onPageBarFocused(lv_event_t* /*event*/) {
    lv_obj_t* current = lvgl_grid_navigation_get_focused(state.pageBar);
    lv_obj_t* target = lv_obj_is_hidden(state.previousButton) ? findFirstButton(state.pageBar) : state.previousButton;
    if (current == nullptr || target == nullptr || target == current) {
        return;
    }
    // Only keys show the selection, the bar's previously focused child can be stale
    lv_indev_t* indev = lv_indev_active();
    const lv_indev_type_t type = indev != nullptr ? lv_indev_get_type(indev) : LV_INDEV_TYPE_NONE;
    const bool showSelection = type == LV_INDEV_TYPE_KEYPAD || type == LV_INDEV_TYPE_ENCODER;
    lv_gridnav_set_focused(state.pageBar, target, LV_ANIM_OFF);
    if (!showSelection) {
        lv_obj_remove_state(target, LV_STATE_FOCUS_KEY);
    }
}

// In the strip, the key towards the app moves the focus to the first widget outside of the strip, and the other key stays at the screen's edge
void onStripKey(lv_event_t* event) {
    const uint32_t key = lv_event_get_key(event);
    const bool right = state.alignment == SYSTEM_BARS_ALIGNMENT_RIGHT;
    if (key == (right ? LV_KEY_LEFT : LV_KEY_RIGHT)) {
        lv_group_t* group = lv_obj_get_group(state.pageBar);
        if (group != nullptr) {
            const uint32_t count = lv_group_get_obj_count(group);
            for (uint32_t i = 0; i < count; i++) {
                lv_group_focus_next(group);
                if (!isInside(lv_group_get_focused(group), state.statusbar)) {
                    break;
                }
            }
        }
        lv_event_stop_processing(event);
    } else if (key == (right ? LV_KEY_RIGHT : LV_KEY_LEFT)) {
        lv_event_stop_processing(event);
    }
}

// The keys visit the page bar after the window's widgets, except in a strip on the left, where they visit it before them
void placePageBarInGroup() {
    lv_group_t* group = lv_obj_get_group(state.pageBar);
    if (group == nullptr) {
        return;
    }
    const uint32_t count = lv_group_get_obj_count(group);
    int32_t current = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (lv_group_get_obj_by_index(group, i) == state.pageBar) {
            current = static_cast<int32_t>(i);
            break;
        }
    }
    if (current < 0) {
        return;
    }
    int32_t target = static_cast<int32_t>(count) - 1;
    if (state.layout == SYSTEM_BARS_LAYOUT_SIDE && state.alignment == SYSTEM_BARS_ALIGNMENT_LEFT) {
        // Before the first widget that isn't in the statusbar
        int32_t index = 0;
        for (uint32_t i = 0; i < count; i++) {
            lv_obj_t* obj = lv_group_get_obj_by_index(group, i);
            if (obj == state.pageBar) {
                continue;
            }
            if (!isInside(obj, state.statusbar)) {
                break;
            }
            index++;
        }
        target = index;
    }
    // Swapping also swaps the focus
    lv_obj_t* focused = lv_group_get_focused(group);
    while (current != target) {
        const int32_t next = current < target ? current + 1 : current - 1;
        lv_group_swap_obj(state.pageBar, lv_group_get_obj_by_index(group, static_cast<uint32_t>(next)));
        current = next;
    }
    if (focused != nullptr && lv_group_get_focused(group) != focused) {
        lv_group_focus_obj(focused);
    }
}

// The target of a pointer device's own events is the device
lv_indev_t* getEventIndev(lv_event_t* event) {
    return static_cast<lv_indev_t*>(lv_event_get_current_target(event));
}

// A swipe down that starts within about 5 mm of the top edge opens the system menu. One dp is 1/160 inch, so 5 mm is 31.5 dp.
int32_t getMenuSwipeZoneHeight() {
    return LV_DPX(32);
}

void onPointerPressed(lv_event_t* event) {
    lv_indev_get_point(getEventIndev(event), &state.pressPoint);
}

// In the side layout, the top edge has no statusbar to swipe down from, so a swipe down that starts there opens the system menu
void onPointerReleased(lv_event_t* event) {
    if (state.layout != SYSTEM_BARS_LAYOUT_SIDE || window_manager_get_active_layer() != WINDOW_LAYER_APP || state.config.show_menu == nullptr) {
        return;
    }
    lv_indev_t* indev = getEventIndev(event);
    lv_point_t point;
    lv_indev_get_point(indev, &point);
    const int32_t edge = getMenuSwipeZoneHeight();
    const int32_t dy = point.y - state.pressPoint.y;
    const int32_t dx = point.x - state.pressPoint.x;
    if (state.pressPoint.y <= edge && dy >= edge * 2 && dy > 2 * LV_ABS(dx)) {
        // The release must not click the widget below the pointer
        lv_indev_reset(indev, nullptr);
        state.config.show_menu();
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

void onResolutionChanged(lv_event_t* /*event*/) {
    system_bars_refresh();
}

// The monochrome theme has no tints, so a line separates the strip from the app
void applySeparator(bool side) {
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_SIDE, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_COLOR, LV_PART_MAIN);
    lv_obj_remove_local_style_prop(state.statusbar, LV_STYLE_BORDER_OPA, LV_PART_MAIN);
    if (side && lvgl_theme_is_mono()) {
        lv_obj_set_style_border_side(state.statusbar, state.alignment == SYSTEM_BARS_ALIGNMENT_RIGHT ? LV_BORDER_SIDE_LEFT : LV_BORDER_SIDE_RIGHT, LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(state.statusbar, 1, LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(state.statusbar, lv_obj_get_style_text_color(state.statusbar, LV_PART_MAIN), LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(state.statusbar, LV_OPA_COVER, LV_STATE_DEFAULT);
    }
}

// The page buttons point left and right in a row, and up and down in the strip
void applyPageButtonIcons(bool side) {
    lv_label_set_text(state.previousIcon, side ? LVGL_ICON_SHARED_KEYBOARD_ARROW_UP : LVGL_ICON_SHARED_KEYBOARD_ARROW_LEFT);
    lv_label_set_text(state.nextIcon, side ? LVGL_ICON_SHARED_KEYBOARD_ARROW_DOWN : LVGL_ICON_SHARED_KEYBOARD_ARROW_RIGHT);
}

// The page bar is below the app container in the split layout, and at the bottom of the statusbar strip in the side layout
void applyPageBarLayout(bool side) {
    lv_obj_t* bar = state.pageBar;
    applyPageButtonIcons(side);
    if (side) {
        lv_obj_set_parent(bar, lvgl_statusbar_get_action_container(state.statusbar));
        lv_obj_set_size(bar, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_COLUMN);
        // The strip has its own background
        lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, LV_STATE_DEFAULT);
        // The buttons are close together, like the statusbar's icons above them
        lv_obj_set_style_pad_row(bar, 2, LV_STATE_DEFAULT);
        // The main action is below the page buttons
        lv_obj_move_to_index(state.mainButton, -1);
        lv_obj_remove_event_cb(bar, onStripKey);
        lv_obj_add_event_cb(bar, onStripKey, static_cast<lv_event_code_t>(LV_EVENT_KEY | LV_EVENT_PREPROCESS), nullptr);
    } else {
        lv_obj_set_parent(bar, state.container);
        lv_obj_move_to_index(bar, lv_obj_get_index(state.appContainer) + 1);
        lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
        const bool right = state.alignment == SYSTEM_BARS_ALIGNMENT_RIGHT;
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, right ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_local_style_prop(bar, LV_STYLE_BG_OPA, LV_PART_MAIN);
        lv_obj_remove_local_style_prop(bar, LV_STYLE_PAD_ROW, LV_PART_MAIN);
        // The main action is before the page buttons when aligned to the left, and after them when aligned to the right
        lv_obj_move_to_index(state.mainButton, right ? -1 : 0);
        lv_obj_remove_event_cb(bar, onStripKey);
    }
    updatePageBarVisibility();
}

// The statusbar is the container's first child: on top in the split layout, and on the left in the side layout
void applyLayout(SystemBarsLayout layout, SystemBarsAlignment alignment) {
    const bool side = layout == SYSTEM_BARS_LAYOUT_SIDE;
    const bool right = alignment == SYSTEM_BARS_ALIGNMENT_RIGHT;
    state.alignment = alignment;
    lvgl_statusbar_set_side(state.statusbar, side);
    lvgl_statusbar_set_side_right(state.statusbar, side && right);
    // The strip is the last child of the container when it's on the right
    lv_obj_move_to_index(state.statusbar, side && right ? -1 : 0);
    applySeparator(side);
    if (side) {
        lv_obj_set_flex_flow(state.container, LV_FLEX_FLOW_ROW);
        lv_obj_set_size(state.appContainer, LV_SIZE_CONTENT, LV_PCT(100));
    } else {
        lv_obj_set_flex_flow(state.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_size(state.appContainer, LV_PCT(100), LV_SIZE_CONTENT);
    }
    lv_obj_set_flex_grow(state.appContainer, 1);
    state.layout = layout;
    applyPageBarLayout(side);
    placePageBarInGroup();
}

// The mono theme grows the border of a pressed or selected button, which would resize the button and move the buttons next to it.
// A fixed size, taken from the icon, keeps the border inside the button.
void fixButtonSize(lv_obj_t* button) {
    lv_point_t size;
    lv_text_get_size(&size, LVGL_ICON_SHARED_KEYBOARD_ARROW_UP, lvgl_get_shared_icon_default_font(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t border = lv_obj_get_style_border_width(button, LV_PART_MAIN);
    lv_obj_set_size(
        button,
        size.x + lv_obj_get_style_pad_left(button, LV_PART_MAIN) + lv_obj_get_style_pad_right(button, LV_PART_MAIN) + 2 * border,
        size.y + lv_obj_get_style_pad_top(button, LV_PART_MAIN) + lv_obj_get_style_pad_bottom(button, LV_PART_MAIN) + 2 * border
    );
}

lv_obj_t* createPageBar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, LV_STATE_DEFAULT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollable(bar, false);
    lvgl_obj_add_edge_padding(bar);
    lv_obj_set_hidden(bar, true);

    state.mainButton = lvgl_icon_button_create(bar);
    state.mainIcon = lv_label_create(state.mainButton);
    lv_obj_set_style_text_font(state.mainIcon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(state.mainButton, onMainPressed, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_set_hidden(state.mainButton, true);

    state.previousButton = lvgl_icon_button_create(bar);
    state.previousIcon = lv_label_create(state.previousButton);
    lv_obj_set_style_text_font(state.previousIcon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(state.previousButton, onPreviousPressed, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_set_hidden(state.previousButton, true);

    state.nextButton = lvgl_icon_button_create(bar);
    state.nextIcon = lv_label_create(state.nextButton);
    lv_obj_set_style_text_font(state.nextIcon, lvgl_get_shared_icon_default_font(), LV_STATE_DEFAULT);
    lv_obj_add_event_cb(state.nextButton, onNextPressed, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_set_hidden(state.nextButton, true);

    // Focusing a button must not scroll the bar or the strip it is in
    for (lv_obj_t* obj : { bar, state.mainButton, state.previousButton, state.nextButton }) {
        lv_obj_set_scroll_on_focus(obj, false);
    }
    for (lv_obj_t* button : { state.mainButton, state.previousButton, state.nextButton }) {
        fixButtonSize(button);
    }
    // The icon stays in the middle when the border grows
    for (lv_obj_t* icon : { state.mainIcon, state.previousIcon, state.nextIcon }) {
        lv_obj_center(icon);
    }

    // The arrow keys move between all buttons of the bar as one row
    lvgl_grid_navigation_add(bar);
    lv_obj_add_event_cb(bar, onPageBarFocused, LV_EVENT_FOCUSED, nullptr);
    return bar;
}

void attach(lv_obj_t* container, lv_obj_t* statusbar, lv_obj_t* appContainer, lv_obj_t* pageBar) {
    state.container = container;
    state.statusbar = statusbar;
    state.appContainer = appContainer;
    state.pageBar = pageBar;
    state.display = lv_obj_get_display(container);
    state.applied = false;
    lv_display_add_event_cb(state.display, onResolutionChanged, LV_EVENT_RESOLUTION_CHANGED, nullptr);
    system_bars_refresh();
}

lv_obj_t* screenInit(lv_obj_t* root) {
    lv_obj_t* container = lv_obj_create(root);
    lv_obj_set_size(container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_gap(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(container, 0, LV_STATE_DEFAULT);
    lv_obj_set_scrollable(container, false);

    lv_obj_t* statusbar = lvgl_statusbar_create(container);

    lv_obj_t* appContainer = lv_obj_create(container);
    lv_obj_set_style_pad_all(appContainer, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(appContainer, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(appContainer, LV_PCT(100));
    lv_obj_set_flex_grow(appContainer, 1);
    lv_obj_set_flex_flow(appContainer, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollable(appContainer, false);

    // Keeps the apps inside the visible area of round displays.
    // On rounded displays, the widgets along the edges move their content inward from the corners instead.
    lv_display_t* display = lv_obj_get_display(root);
    DisplayShape shape;
    lvgl_display_get_shape(display, &shape);
    if (shape.shape == DISPLAY_SHAPE_CIRCLE) {
        LvglInsets insets;
        lvgl_display_get_insets(display, &insets);
        lv_obj_update_layout(statusbar);
        lv_obj_set_style_pad_top(appContainer, std::max<int32_t>(0, insets.top - lv_obj_get_height(statusbar)), LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(appContainer, insets.bottom, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(appContainer, insets.left, LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(appContainer, insets.right, LV_STATE_DEFAULT);
    }

    lv_obj_t* pageBar = createPageBar(container);
    attach(container, statusbar, appContainer, pageBar);

    if (state.config.on_screen_created != nullptr) {
        state.config.on_screen_created(root);
    }
    return appContainer;
}

} // namespace

void system_bars_window_built() {
    if (state.pageBar != nullptr) {
        placePageBarInGroup();
    }
}

void system_bars_window_deleted() {
    if (state.pageBar == nullptr) {
        return;
    }
    // What the window set is gone with its widgets
    lv_obj_set_hidden(state.mainButton, true);
    state.onMain = nullptr;
    state.mainUserData = nullptr;
    state.onPrevious = nullptr;
    state.onNext = nullptr;
    state.pageUserData = nullptr;
    system_bars_set_page_buttons_visible(false);
}

void system_bars_detach() {
    if (state.display != nullptr) {
        lv_display_remove_event_cb_with_user_data(state.display, onResolutionChanged, nullptr);
        forEachPointer(removePointerCallbacks);
    }
    state.container = nullptr;
    state.statusbar = nullptr;
    state.appContainer = nullptr;
    state.pageBar = nullptr;
    state.mainButton = nullptr;
    state.mainIcon = nullptr;
    state.previousButton = nullptr;
    state.previousIcon = nullptr;
    state.nextButton = nullptr;
    state.nextIcon = nullptr;
    state.display = nullptr;
    state.layout = SYSTEM_BARS_LAYOUT_SPLIT;
    state.alignment = SYSTEM_BARS_ALIGNMENT_LEFT;
    state.onMain = nullptr;
    state.mainUserData = nullptr;
    state.onPrevious = nullptr;
    state.onNext = nullptr;
    state.pageUserData = nullptr;
}

extern "C" {

void system_bars_configure(const struct SystemBarsConfig* config) {
    state.config = *config;
    window_manager_configure(screenInit);
}

void system_bars_refresh(void) {
    if (state.container == nullptr) {
        return;
    }
    // Also picks up pointer devices that were added since the last refresh
    forEachPointer(addPointerCallbacks);
    const auto width = lv_display_get_horizontal_resolution(state.display);
    const auto height = lv_display_get_vertical_resolution(state.display);
    auto layout = state.config.resolve_layout != nullptr ? state.config.resolve_layout(width, height) : SYSTEM_BARS_LAYOUT_SPLIT;
    // Circular displays only have room for the statusbar at the top
    if (lvgl_statusbar_get_action_container(state.statusbar) == nullptr) {
        layout = SYSTEM_BARS_LAYOUT_SPLIT;
    }
    const auto alignment = state.config.resolve_alignment != nullptr ? state.config.resolve_alignment() : SYSTEM_BARS_ALIGNMENT_LEFT;
    if (state.applied && layout == state.layout && alignment == state.alignment) {
        return;
    }
    applyLayout(layout, alignment);
    state.applied = true;
}

void system_bars_set_statusbar_visible(bool visible) {
    if (state.statusbar != nullptr) {
        lv_obj_set_hidden(state.statusbar, !visible);
    }
}

enum SystemBarsLayout system_bars_get_layout(void) {
    return state.layout;
}

enum SystemBarsAlignment system_bars_get_alignment(void) {
    return state.alignment;
}

void system_bars_set_main_action(const char* icon, SystemBarsCallback on_pressed, void* user_data) {
    if (state.pageBar == nullptr) {
        return;
    }
    state.onMain = on_pressed;
    state.mainUserData = user_data;
    if (icon != nullptr) {
        lv_label_set_text(state.mainIcon, icon);
    }
    lv_obj_set_hidden(state.mainButton, icon == nullptr);
    updatePageBarVisibility();
}

void system_bars_set_page_button_callbacks(SystemBarsCallback on_previous, SystemBarsCallback on_next, void* user_data) {
    state.onPrevious = on_previous;
    state.onNext = on_next;
    state.pageUserData = user_data;
}

bool system_bars_is_menu_swipe_zone_pressed(void) {
    return state.display != nullptr && state.layout == SYSTEM_BARS_LAYOUT_SIDE && state.pressPoint.y <= getMenuSwipeZoneHeight();
}

void system_bars_set_page_buttons_visible(bool visible) {
    if (state.pageBar == nullptr) {
        return;
    }
    lv_obj_set_hidden(state.previousButton, !visible);
    lv_obj_set_hidden(state.nextButton, !visible);
    updatePageBarVisibility();
}

}
