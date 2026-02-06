#include <tt_app.h>
#include <tt_lvgl_toolbar.h>

#include <tactility/concurrent/dispatcher.h>
#include <tactility/concurrent/event_group.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/concurrent/recursive_mutex.h>
#include <tactility/concurrent/thread.h>
#include <tactility/concurrent/timer.h>
#include <tactility/drivers/gpio.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/drivers/root.h>
#include <tactility/check.h>
#include <tactility/defines.h>
#include <tactility/delay.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/error.h>
#include <tactility/log.h>
#include <tactility/module.h>
#include <tactility/time.h>

#include <tactility/lvgl_module.h>

static void onShowApp(AppHandle app, void* data, lv_obj_t* parent) {
    lv_obj_t* toolbar = tt_lvgl_toolbar_create_for_app(parent, app);
    lv_obj_align(toolbar, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, "Hello, world!");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
}

int main(int argc, char* argv[]) {
    tt_app_register((AppRegistration) {
        .onShow = onShowApp
    });
    return 0;
}
