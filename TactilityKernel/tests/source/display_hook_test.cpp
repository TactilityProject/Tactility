#include "doctest.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/display.h>

static int driver_draw_count = 0;
static int driver_swap_count = 0;
static const void* seen_by_hook_before_driver = nullptr;
static int hook_draw_count = 0;
static int hook_swap_count = 0;
static int hook_clear_count = 0;
static bool hook_swap_value = false;
static int draw_order = 0;
static int hook_draw_order = 0;
static int driver_draw_order = 0;

static error_t fake_draw_bitmap(Device*, int32_t, int32_t, int32_t, int32_t, const void*) {
    driver_draw_count++;
    driver_draw_order = ++draw_order;
    return ERROR_NONE;
}

// Only swapping to true succeeds
static error_t fake_swap_xy(Device*, bool swap_axes) {
    driver_swap_count++;
    return swap_axes ? ERROR_NONE : ERROR_RESOURCE;
}

static error_t fake_clear(Device*) {
    return ERROR_NONE;
}

static const DisplayApi fake_display_api = {
    .capabilities = 0,
    .reset = nullptr,
    .init = nullptr,
    .draw_bitmap = fake_draw_bitmap,
    .clear = fake_clear,
    .refresh = nullptr,
    .mirror = nullptr,
    .swap_xy = fake_swap_xy,
};

static Driver fake_display_driver = {
    .name = "fake_display_driver",
    .compatible = (const char*[]) { "display_hook_test,fake", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &fake_display_api,
    .device_type = &DISPLAY_TYPE,
    .internal = nullptr,
};

static void hook_on_draw_bitmap(Device*, int32_t, int32_t, int32_t, int32_t, const void* color_data) {
    hook_draw_count++;
    hook_draw_order = ++draw_order;
    seen_by_hook_before_driver = color_data;
}

static void hook_on_swap_xy(Device*, bool swap_axes) {
    hook_swap_count++;
    hook_swap_value = swap_axes;
}

static void hook_on_clear(Device*) {
    hook_clear_count++;
}

static const DisplayHook full_hook = {
    .on_draw_bitmap = hook_on_draw_bitmap,
    .on_clear = hook_on_clear,
    .on_swap_xy = hook_on_swap_xy,
};

// Only on_swap_xy is set, every other member must be skipped
static const DisplayHook partial_hook = {
    .on_swap_xy = hook_on_swap_xy,
};

TEST_CASE("display_set_hook observes display calls") {
    driver_draw_count = driver_swap_count = hook_draw_count = hook_swap_count = hook_clear_count = 0;
    draw_order = hook_draw_order = driver_draw_order = 0;
    seen_by_hook_before_driver = nullptr;

    static Device fake_device {
        .name = "fake_display_device",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_display_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "display_hook_test,fake"), ERROR_NONE);

    uint16_t pixel = 0;

    // No hook installed
    CHECK_EQ(display_draw_bitmap(&fake_device, 0, 0, 1, 1, &pixel), ERROR_NONE);
    CHECK_EQ(hook_draw_count, 0);

    display_set_hook(&full_hook);

    CHECK_EQ(display_draw_bitmap(&fake_device, 0, 0, 1, 1, &pixel), ERROR_NONE);
    CHECK_EQ(hook_draw_count, 1);
    CHECK_EQ(driver_draw_count, 2);
    CHECK_EQ(seen_by_hook_before_driver, &pixel);
    // The hook ran before the driver for the same call
    CHECK_LT(hook_draw_order, driver_draw_order);

    CHECK_EQ(display_swap_xy(&fake_device, true), ERROR_NONE);
    CHECK_EQ(hook_swap_count, 1);
    CHECK(hook_swap_value);

    // The hook is not called when the driver call fails
    CHECK_EQ(display_swap_xy(&fake_device, false), ERROR_RESOURCE);
    CHECK_EQ(hook_swap_count, 1);

    CHECK_EQ(display_clear(&fake_device), ERROR_NONE);
    CHECK_EQ(hook_clear_count, 1);

    // Members that are null are skipped
    display_set_hook(&partial_hook);
    CHECK_EQ(display_draw_bitmap(&fake_device, 0, 0, 1, 1, &pixel), ERROR_NONE);
    CHECK_EQ(display_clear(&fake_device), ERROR_NONE);
    CHECK_EQ(hook_draw_count, 1);
    CHECK_EQ(hook_clear_count, 1);
    CHECK_EQ(display_swap_xy(&fake_device, true), ERROR_NONE);
    CHECK_EQ(hook_swap_count, 2);

    // A removed hook is not called anymore
    display_set_hook(nullptr);
    CHECK_EQ(display_swap_xy(&fake_device, true), ERROR_NONE);
    CHECK_EQ(hook_swap_count, 2);

    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_display_driver), ERROR_NONE);
}
