#include "doctest.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/keyboard.h>
#include <tactility/module.h>

TEST_CASE("keyboard_key_from_hid_usage maps letters and digits") {
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x04, false, false), (uint32_t)'a');
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x1D, false, false), (uint32_t)'z');
    CHECK_EQ(keyboard_key_from_hid_usage(KEYBOARD_HID_MOD_LEFT_SHIFT, 0x04, false, false), (uint32_t)'A');
    // Right Shift behaves like Left Shift
    CHECK_EQ(keyboard_key_from_hid_usage(KEYBOARD_HID_MOD_RIGHT_SHIFT, 0x04, false, false), (uint32_t)'A');
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x1E, false, false), (uint32_t)'1');
    CHECK_EQ(keyboard_key_from_hid_usage(KEYBOARD_HID_MOD_LEFT_SHIFT, 0x1E, false, false), (uint32_t)'!');
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x2C, false, false), (uint32_t)' ');
}

TEST_CASE("keyboard_key_from_hid_usage applies caps lock to letters only") {
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x04, true, false), (uint32_t)'A');
    // Caps Lock + Shift on a letter cancels out
    CHECK_EQ(keyboard_key_from_hid_usage(KEYBOARD_HID_MOD_LEFT_SHIFT, 0x04, true, false), (uint32_t)'a');
    // Caps Lock does not affect non-letters
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x1E, true, false), (uint32_t)'1');
}

TEST_CASE("keyboard_key_from_hid_usage maps control keys to CodePoint values") {
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x28, false, false), (uint32_t)CODEPOINT_ENTER);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x29, false, false), (uint32_t)CODEPOINT_ESCAPE);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x2A, false, false), (uint32_t)CODEPOINT_BACKSPACE);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x2B, false, false), (uint32_t)CODEPOINT_TAB);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x4C, false, false), (uint32_t)CODEPOINT_DELETE);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x52, false, false), (uint32_t)CODEPOINT_ARROW_UP);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x51, false, false), (uint32_t)CODEPOINT_ARROW_DOWN);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x50, false, false), (uint32_t)CODEPOINT_ARROW_LEFT);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x4F, false, false), (uint32_t)CODEPOINT_ARROW_RIGHT);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x4A, false, false), (uint32_t)CODEPOINT_HOME);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x4D, false, false), (uint32_t)CODEPOINT_END);
}

TEST_CASE("keyboard_key_from_hid_usage maps keypad with num lock") {
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x62, false, true), (uint32_t)'0');
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x59, false, true), (uint32_t)'1');
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x57, false, false), (uint32_t)'+');
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x58, false, false), (uint32_t)CODEPOINT_ENTER);
    // Without num lock the keypad doubles as navigation
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x62, false, false), 0u);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x5A, false, false), (uint32_t)CODEPOINT_ARROW_DOWN);
}

TEST_CASE("keyboard_key_from_hid_usage does not suppress ctrl/alt chords") {
    CHECK_EQ(keyboard_key_from_hid_usage(KEYBOARD_HID_MOD_LEFT_CTRL, 0x06, false, false), (uint32_t)'c');
    CHECK_EQ(keyboard_key_from_hid_usage(KEYBOARD_HID_MOD_LEFT_ALT, 0x06, false, false), (uint32_t)'c');
}

TEST_CASE("keyboard_key_from_hid_usage returns 0 for unmapped keys") {
    // Reserved usage and function keys have no codepoint mapping here
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x00, false, false), 0u);
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0x3A, false, false), 0u); // F1
    CHECK_EQ(keyboard_key_from_hid_usage(0, 0xE0, false, false), 0u); // Left Ctrl itself
}

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

TEST_CASE("keyboard_read_key returns ERROR_INVALID_STATE, without touching the driver, on a device that isn't started") {
    read_key_call_count = 0;

    static Device fake_device {
        .name = "fake_keyboard_device_not_started",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_keyboard_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "keyboard_test,fake"), ERROR_NONE);
    // Deliberately not started - a hotplug-managed keyboard can be constructed+added long before
    // (or after) its driver is actually running, but LVGL's indev still polls it unconditionally.

    KeyboardKeyData out { .key = 'x' };
    CHECK_EQ(keyboard_read_key(&fake_device, &out), ERROR_INVALID_STATE);
    CHECK_EQ(out.key, 0);
    CHECK_EQ(read_key_call_count, 0);

    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_keyboard_driver), ERROR_NONE);
}

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

TEST_CASE("keyboard_read_key fans out to subscribers even when called directly") {
    read_key_call_count = 0;

    static Device fake_device {
        .name = "fake_keyboard_device_read_key_fanout",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_keyboard_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "keyboard_test,fake"), ERROR_NONE);
    CHECK_EQ(device_start(&fake_device), ERROR_NONE);

    KeyboardEventSubscription sub {};
    CHECK_EQ(keyboard_subscribe(&fake_device, &sub), ERROR_NONE);

    KeyboardKeyData out {};
    CHECK_EQ(keyboard_read_key(&fake_device, &out), ERROR_NONE);
    CHECK_EQ(out.key, 'a');

    // sub never called keyboard_poll() itself - it received this via keyboard_read_key()'s fan-out.
    KeyboardKeyData polled {};
    CHECK_EQ(keyboard_poll(&fake_device, &sub, &polled), ERROR_NONE);
    CHECK_EQ(polled.key, 'a');
    CHECK_EQ(read_key_call_count, 1);

    CHECK_EQ(keyboard_unsubscribe(&fake_device, &sub), ERROR_NONE);
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
