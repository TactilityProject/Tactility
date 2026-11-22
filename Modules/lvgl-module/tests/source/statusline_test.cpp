#include "doctest.h"

#include <lvgl.h>
#include <lvgl/widgets/statusline.h>

#include <string>

TEST_CASE("lvgl_statusline shows its level as a state of the dot and keeps one line of text") {
    lv_init();
    lv_display_create(320, 240);
    lv_obj_t* statusline = lvgl_statusline_create(lv_screen_active());
    lv_obj_t* dot = lv_obj_get_child(statusline, 0);
    lv_obj_t* label = lv_obj_get_child(statusline, 1);

    CHECK_FALSE(lv_obj_has_state(dot, LV_STATE_USER_1));
    CHECK_FALSE(lv_obj_has_state(dot, LV_STATE_USER_2));

    lvgl_statusline_set_level(statusline, LVGL_STATUSLINE_LEVEL_WARNING);
    CHECK(lv_obj_has_state(dot, LV_STATE_USER_1));
    CHECK_FALSE(lv_obj_has_state(dot, LV_STATE_USER_2));

    lvgl_statusline_set_level(statusline, LVGL_STATUSLINE_LEVEL_ERROR);
    CHECK_FALSE(lv_obj_has_state(dot, LV_STATE_USER_1));
    CHECK(lv_obj_has_state(dot, LV_STATE_USER_2));

    lvgl_statusline_set_level(statusline, LVGL_STATUSLINE_LEVEL_OK);
    CHECK_FALSE(lv_obj_has_state(dot, LV_STATE_USER_1));
    CHECK_FALSE(lv_obj_has_state(dot, LV_STATE_USER_2));

    lvgl_statusline_set_text(statusline, "A status that is much too long to fit on one line of a small display");
    lv_obj_update_layout(statusline);
    CHECK_EQ(std::string(lv_label_get_text(label)).substr(0, 8), "A status");
    // One line high, also with text that doesn't fit
    CHECK_EQ(lv_obj_get_height(label), lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));

    lv_obj_delete(statusline);
    lv_deinit();
}
