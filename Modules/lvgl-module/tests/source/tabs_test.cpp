#include "doctest.h"

#include <lvgl.h>
#include <lvgl/fonts.h>
#include <lvgl/widgets/tabs.h>

namespace {

struct Fixture {
    lv_obj_t* parent;
    lv_obj_t* tabs;

    explicit Fixture(int32_t parentWidth, LvglTabsMode mode) {
        lv_init();
        lv_display_create(320, 240);
        parent = lv_obj_create(lv_screen_active());
        lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);
        lv_obj_set_size(parent, parentWidth, 100);
        tabs = lvgl_tabs_create(parent);
        lvgl_tabs_set_mode(tabs, mode);
        lvgl_tabs_add_tab(tabs, "A");
        lvgl_tabs_add_tab(tabs, "A much longer tab label");
        lv_obj_update_layout(parent);
    }

    ~Fixture() {
        lv_obj_delete(parent);
        lv_deinit();
    }

    int32_t tabWidth(int32_t index) const { return lv_obj_get_width(lv_obj_get_child(tabs, index)); }
};

}

TEST_CASE("lvgl_tabs in wrap mode is as wide as its tabs when they fit") {
    Fixture fixture(320, LVGL_TABS_MODE_WRAP);
    CHECK_LT(lv_obj_get_width(fixture.tabs), 320);
    CHECK_LT(fixture.tabWidth(0), fixture.tabWidth(1));
}

TEST_CASE("lvgl_tabs in wrap mode shortens the long tabs to fit in its parent") {
    Fixture wide(320, LVGL_TABS_MODE_WRAP);
    const int32_t shortWidth = wide.tabWidth(0);
    const int32_t longWidth = wide.tabWidth(1);

    Fixture narrow(80, LVGL_TABS_MODE_WRAP);
    CHECK_LE(lv_obj_get_width(narrow.tabs), 80);
    CHECK_EQ(narrow.tabWidth(0), shortWidth);
    CHECK_LT(narrow.tabWidth(1), longWidth);
}

TEST_CASE("lvgl_tabs in wrap mode follows the parent's width") {
    Fixture fixture(80, LVGL_TABS_MODE_WRAP);
    const int32_t narrowWidth = fixture.tabWidth(1);
    lv_obj_set_width(fixture.parent, 320);
    lv_obj_update_layout(fixture.parent);
    CHECK_GT(fixture.tabWidth(1), narrowWidth);
}

TEST_CASE("lvgl_tabs in wrap mode in a row fits in the room that the other widgets leave") {
    Fixture fixture(150, LVGL_TABS_MODE_WRAP);
    lv_obj_set_flex_flow(fixture.parent, LV_FLEX_FLOW_ROW);
    lv_obj_t* sibling = lv_obj_create(fixture.parent);
    lv_obj_set_size(sibling, 100, 20);
    lv_obj_update_layout(fixture.parent);
    CHECK_LE(lv_obj_get_width(fixture.tabs) + 100, 150);

    // Hiding the other widget gives its room to the tabs
    const int32_t narrowWidth = fixture.tabWidth(1);
    lv_obj_set_hidden(sibling, true);
    lv_obj_update_layout(fixture.parent);
    CHECK_GT(fixture.tabWidth(1), narrowWidth);
}

TEST_CASE("lvgl_tabs hides a tab and selects another one when the selected tab is hidden") {
    Fixture fixture(320, LVGL_TABS_MODE_WRAP);
    lvgl_tabs_set_selected(fixture.tabs, 1);
    lvgl_tabs_set_tab_hidden(fixture.tabs, 1, true);
    lv_obj_update_layout(fixture.parent);
    CHECK(lv_obj_is_hidden(lv_obj_get_child(fixture.tabs, 1)));
    CHECK_EQ(lvgl_tabs_get_selected(fixture.tabs), 0);
    // The bar only wraps the tabs that are shown
    CHECK_EQ(lv_obj_get_width(fixture.tabs), fixture.tabWidth(0));

    lvgl_tabs_set_tab_hidden(fixture.tabs, 1, false);
    lv_obj_update_layout(fixture.parent);
    CHECK_FALSE(lv_obj_is_hidden(lv_obj_get_child(fixture.tabs, 1)));
}

TEST_CASE("lvgl_tabs uses the small font before it shortens labels") {
    lv_init();
    lv_display_create(320, 240);
    lvgl_set_text_font(FONT_SIZE_SMALL, &lv_font_montserrat_14, lv_font_get_line_height(&lv_font_montserrat_14));
    lv_obj_t* parent = lv_obj_create(lv_screen_active());
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);
    lv_obj_t* tabs = lvgl_tabs_create(parent);
    // The regular font is larger than the small one
    lv_obj_set_style_text_font(tabs, &lv_font_montserrat_24, LV_PART_MAIN);
    lvgl_tabs_set_mode(tabs, LVGL_TABS_MODE_WRAP);
    lv_obj_t* tab = lvgl_tabs_add_tab(tabs, "Installed");

    lv_obj_set_size(parent, 300, 100);
    lv_obj_update_layout(parent);
    const int32_t regularWidth = lv_obj_get_width(tab);
    CHECK_EQ(lv_obj_get_style_text_font(tab, LV_PART_MAIN), &lv_font_montserrat_24);

    // Narrower than the regular label, but wide enough for the small one
    lv_obj_set_width(parent, regularWidth - 10);
    lv_obj_update_layout(parent);
    CHECK_EQ(lv_obj_get_style_text_font(tab, LV_PART_MAIN), &lv_font_montserrat_14);
    CHECK_LT(lv_obj_get_width(tab), regularWidth - 10);

    lv_obj_set_width(parent, 300);
    lv_obj_update_layout(parent);
    CHECK_EQ(lv_obj_get_style_text_font(tab, LV_PART_MAIN), &lv_font_montserrat_24);

    lv_obj_delete(parent);
    lv_deinit();
}

TEST_CASE("lvgl_tabs shows a badge on a tab without changing its size") {
    Fixture fixture(320, LVGL_TABS_MODE_WRAP);
    const int32_t width = fixture.tabWidth(1);
    lv_obj_t* tab = lv_obj_get_child(fixture.tabs, 1);

    lvgl_tabs_set_tab_badge(fixture.tabs, 1, true);
    // Showing it again doesn't add another one
    lvgl_tabs_set_tab_badge(fixture.tabs, 1, true);
    lv_obj_update_layout(fixture.parent);
    CHECK_EQ(lv_obj_get_child_count(tab), 2);
    CHECK_EQ(fixture.tabWidth(1), width);

    lvgl_tabs_set_tab_badge(fixture.tabs, 1, false);
    CHECK_EQ(lv_obj_get_child_count(tab), 1);
}

TEST_CASE("lvgl_tabs shows only the icons of tabs that have too little room for their labels") {
    lv_init();
    lv_display_create(320, 240);
    lv_obj_t* parent = lv_obj_create(lv_screen_active());
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(parent, 0, LV_PART_MAIN);
    lv_obj_set_size(parent, 300, 100);
    lv_obj_t* tabs = lvgl_tabs_create(parent);
    lvgl_tabs_set_mode(tabs, LVGL_TABS_MODE_WRAP);
    lv_obj_t* tab = lvgl_tabs_add_tab_with_icon(tabs, "I", "A label that is long");
    lv_obj_update_layout(parent);
    // The icon is the first child and the label the second one
    lv_obj_t* label = lv_obj_get_child(tab, 1);
    CHECK_FALSE(lv_obj_is_hidden(label));
    const int32_t wideWidth = lv_obj_get_width(tab);

    lv_obj_set_width(parent, 40);
    lv_obj_update_layout(parent);
    CHECK(lv_obj_is_hidden(label));
    CHECK_LT(lv_obj_get_width(tab), wideWidth);

    lv_obj_set_width(parent, 300);
    lv_obj_update_layout(parent);
    CHECK_FALSE(lv_obj_is_hidden(label));

    lv_obj_delete(parent);
    lv_deinit();
}

TEST_CASE("lvgl_tabs in fixed mode fills the parent with equal tabs") {
    Fixture fixture(320, LVGL_TABS_MODE_FIXED);
    CHECK_EQ(lv_obj_get_width(fixture.tabs), 320);
    CHECK_EQ(fixture.tabWidth(0), fixture.tabWidth(1));
}

TEST_CASE("lvgl_tabs selects the first tab, and a click selects another tab") {
    Fixture fixture(320, LVGL_TABS_MODE_FIXED);
    CHECK_EQ(lvgl_tabs_get_selected(fixture.tabs), 0);

    int changes = 0;
    lv_obj_add_event_cb(fixture.tabs, [](lv_event_t* event) {
        (*static_cast<int*>(lv_event_get_user_data(event)))++;
    }, LV_EVENT_VALUE_CHANGED, &changes);

    lv_obj_send_event(lv_obj_get_child(fixture.tabs, 1), LV_EVENT_CLICKED, nullptr);
    CHECK_EQ(lvgl_tabs_get_selected(fixture.tabs), 1);
    CHECK_EQ(changes, 1);

    // Selecting in code doesn't send an event
    lvgl_tabs_set_selected(fixture.tabs, 0);
    CHECK_EQ(lvgl_tabs_get_selected(fixture.tabs), 0);
    CHECK_EQ(changes, 1);
}
