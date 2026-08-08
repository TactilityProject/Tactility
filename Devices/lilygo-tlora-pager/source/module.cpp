#include <lvgl/lvgl.h>
#include <tactility/module.h>
#include <tactility/system_event.h>

#include <lilygo/drivers/tpager_encoder_input.h>

constexpr auto* TAG = "T-Lora Pager";

extern "C" {

static void on_boot_completed(struct SystemEvent* /*event*/, void* /*context*/) {
    // The kernel tpager_encoder device is already started by kernel_init(); this just
    // registers it as an LVGL input device, which requires LVGL to be up first.
    lvgl_lock();
    tpager_encoder::init();
    lvgl_unlock();
}

static error_t start() {
    system_event_callback_add(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed, nullptr);
    return ERROR_NONE;
}

static error_t stop() {
    system_event_callback_remove(KERNEL_EVENT_BOOT_COMPLETED, on_boot_completed);
    return ERROR_NONE;
}

Module lilygo_tlora_pager_module = {
    .name = "lilygo-tlora-pager",
    .start = start,
    .stop = stop
};

}
