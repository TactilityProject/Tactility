#include "doctest.h"
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/module.h>

static Module keyboard_test_module = {
    .name = "keyboard_test_module",
    .start = nullptr,
    .stop = nullptr
};

static int read_key_call_count = 0;

// First call reports a key press, every call after that reports nothing pending (key == 0),
// matching the convention every real driver uses (see e.g. sdl_keyboard_read_key).
static error_t fake_read_key(Device*, KeyboardKeyData* data) {
    read_key_call_count++;
    if (read_key_call_count == 1) {
        data->key = 'a';
        data->pressed = true;
        data->continue_reading = false;
    } else {
        data->key = 0;
        data->pressed = false;
        data->continue_reading = false;
    }
    return ERROR_NONE;
}

static const KeyboardApi fake_keyboard_api = {
    .read_key = fake_read_key,
    .get_backlight = nullptr,
    .is_present = nullptr,
};

static Driver fake_keyboard_driver = {
    .name = "fake_keyboard_driver",
    .compatible = (const char*[]) { "keyboard_test,fake", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &fake_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &keyboard_test_module,
    .internal = nullptr,
};

// No read_key: this driver only ever pushes events via keyboard_emit_key().
static const KeyboardApi fake_emit_only_keyboard_api = {
    .read_key = nullptr,
    .get_backlight = nullptr,
    .is_present = nullptr,
};

static Driver fake_emit_only_keyboard_driver = {
    .name = "fake_emit_only_keyboard_driver",
    .compatible = (const char*[]) { "keyboard_test,fake_emit_only", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &fake_emit_only_keyboard_api,
    .device_type = &KEYBOARD_TYPE,
    .owner = &keyboard_test_module,
    .internal = nullptr,
};

TEST_CASE("keyboard_poll's default implementation fans out a read_key result to other subscribers") {
    read_key_call_count = 0;

    static Device fake_device {
        .name = "fake_keyboard_device",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_keyboard_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "keyboard_test,fake"), ERROR_NONE);
    CHECK_EQ(device_start(&fake_device), ERROR_NONE);

    KeyboardEventSubscription sub_a {};
    KeyboardEventSubscription sub_b {};
    CHECK_EQ(keyboard_subscribe(&fake_device, &sub_a), ERROR_NONE);
    CHECK_EQ(keyboard_subscribe(&fake_device, &sub_b), ERROR_NONE);

    KeyboardKeyData out {};
    CHECK_EQ(keyboard_poll(&fake_device, &sub_a, &out), ERROR_NONE);
    CHECK_EQ(out.key, 'a');
    CHECK_EQ(read_key_call_count, 1);

    // sub_b receives the same event without a second hardware read - it was fanned out by sub_a's poll.
    CHECK_EQ(keyboard_poll(&fake_device, &sub_b, &out), ERROR_NONE);
    CHECK_EQ(out.key, 'a');
    CHECK_EQ(read_key_call_count, 1);

    // sub_b's queue is now empty; polling again falls through to a real (empty) hardware read.
    CHECK_EQ(keyboard_poll(&fake_device, &sub_b, &out), ERROR_TIMEOUT);
    CHECK_EQ(read_key_call_count, 2);

    CHECK_EQ(keyboard_unsubscribe(&fake_device, &sub_a), ERROR_NONE);
    CHECK_EQ(keyboard_unsubscribe(&fake_device, &sub_b), ERROR_NONE);
    CHECK_EQ(device_stop(&fake_device), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_keyboard_driver), ERROR_NONE);
}

TEST_CASE("keyboard_emit_key delivers to subscribers of a driver with no read_key") {
    static Device fake_device {
        .name = "fake_emit_only_keyboard_device",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_emit_only_keyboard_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "keyboard_test,fake_emit_only"), ERROR_NONE);
    CHECK_EQ(device_start(&fake_device), ERROR_NONE);

    KeyboardEventSubscription sub {};
    CHECK_EQ(keyboard_subscribe(&fake_device, &sub), ERROR_NONE);

    // Nothing pushed yet, and there's no read_key to fall back on.
    KeyboardKeyData out {};
    CHECK_EQ(keyboard_poll(&fake_device, &sub, &out), ERROR_TIMEOUT);

    KeyboardKeyData emitted { .key = 'z', .pressed = true, .continue_reading = false };
    keyboard_emit_key(&fake_device, emitted);

    CHECK_EQ(keyboard_poll(&fake_device, &sub, &out), ERROR_NONE);
    CHECK_EQ(out.key, 'z');

    CHECK_EQ(keyboard_unsubscribe(&fake_device, &sub), ERROR_NONE);
    CHECK_EQ(device_stop(&fake_device), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_emit_only_keyboard_driver), ERROR_NONE);
}

TEST_CASE("keyboard_unsubscribe stops a subscription from receiving further fanned-out events") {
    read_key_call_count = 0;

    static Device fake_device {
        .name = "fake_keyboard_device_2",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_keyboard_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "keyboard_test,fake"), ERROR_NONE);
    CHECK_EQ(device_start(&fake_device), ERROR_NONE);

    KeyboardEventSubscription sub_a {};
    KeyboardEventSubscription sub_b {};
    CHECK_EQ(keyboard_subscribe(&fake_device, &sub_a), ERROR_NONE);
    CHECK_EQ(keyboard_subscribe(&fake_device, &sub_b), ERROR_NONE);
    CHECK_EQ(keyboard_unsubscribe(&fake_device, &sub_b), ERROR_NONE);

    KeyboardKeyData out {};
    CHECK_EQ(keyboard_poll(&fake_device, &sub_a, &out), ERROR_NONE);
    CHECK_EQ(out.key, 'a');

    // sub_b was unsubscribed before the poll, so nothing was fanned out to it.
    CHECK_EQ(keyboard_poll(&fake_device, &sub_b, &out), ERROR_TIMEOUT);

    CHECK_EQ(keyboard_unsubscribe(&fake_device, &sub_a), ERROR_NONE);
    CHECK_EQ(device_stop(&fake_device), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_keyboard_driver), ERROR_NONE);
}
