#include "doctest.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/touchbar.h>

static int read_state_call_count = 0;
static int recalibrate_call_count = 0;

static error_t fake_read_state(Device*, TouchbarState* state) {
    read_state_call_count++;
    *state = {};
    state->channel_count = 3;
    state->position = 120;
    state->position_max = 200;
    return ERROR_NONE;
}

static error_t fake_recalibrate(Device*) {
    recalibrate_call_count++;
    return ERROR_NONE;
}

static constexpr uint32_t FAKE_CAPABILITIES = TOUCHBAR_CAPABILITY_POSITION | TOUCHBAR_CAPABILITY_GESTURE_SWIPE;

static const TouchbarApi fake_touchbar_api = {
    .capabilities = FAKE_CAPABILITIES,
    .read_state = fake_read_state,
    .recalibrate = fake_recalibrate,
};

// A chip without an optional function, like a plain button controller
static bool fake_has_capability(Device*, uint32_t capability) {
    return capability == TOUCHBAR_CAPABILITY_GESTURE_HOLD;
}

static const TouchbarApi dynamic_touchbar_api = {
    .capabilities = 0,
    .read_state = fake_read_state,
    .has_capability = fake_has_capability,
};

static const TouchbarApi minimal_touchbar_api = {
    .capabilities = 0,
    .read_state = fake_read_state,
    .recalibrate = nullptr,
};

static Driver fake_touchbar_driver = {
    .name = "fake_touchbar_driver",
    .compatible = (const char*[]) { "touchbar_test,fake", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &fake_touchbar_api,
    .device_type = &TOUCHBAR_TYPE,
    .internal = nullptr,
};

static Driver minimal_touchbar_driver = {
    .name = "minimal_touchbar_driver",
    .compatible = (const char*[]) { "touchbar_test,minimal", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &minimal_touchbar_api,
    .device_type = &TOUCHBAR_TYPE,
    .internal = nullptr,
};

static Driver dynamic_touchbar_driver = {
    .name = "dynamic_touchbar_driver",
    .compatible = (const char*[]) { "touchbar_test,dynamic", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = &dynamic_touchbar_api,
    .device_type = &TOUCHBAR_TYPE,
    .internal = nullptr,
};

static constexpr uint16_t TEST_POSITION = 77;
static constexpr uint16_t TEST_TOUCH_MASK = 0b010;
static constexpr uint8_t TEST_CHANNEL = 1;

static TouchbarEvent make_event(TouchbarEventType type, uint16_t position) {
    return TouchbarEvent { .type = type, .direction = 0, .channel = 0, .touch_mask = 0, .position = position };
}

TEST_CASE("touchbar_read_state returns ERROR_INVALID_STATE, without touching the driver, on a device that isn't started") {
    read_state_call_count = 0;

    static Device fake_device {
        .name = "fake_touchbar_device_not_started",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);

    TouchbarState state { .channel_count = 1 };
    CHECK_EQ(touchbar_read_state(&fake_device, &state), ERROR_INVALID_STATE);
    CHECK_EQ(state.channel_count, 0);
    CHECK_EQ(read_state_call_count, 0);

    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_read_state forwards to the driver of a started device") {
    read_state_call_count = 0;

    static Device fake_device {
        .name = "fake_touchbar_device_read_state",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);
    CHECK_EQ(device_start(&fake_device), ERROR_NONE);

    TouchbarState state {};
    CHECK_EQ(touchbar_read_state(&fake_device, &state), ERROR_NONE);
    CHECK_EQ(state.channel_count, 3);
    CHECK_EQ(state.position, 120);
    CHECK_EQ(state.position_max, 200);
    CHECK_EQ(read_state_call_count, 1);

    CHECK_EQ(device_stop(&fake_device), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_emit_event delivers in order to every subscriber of the device only") {
    static Device fake_device {
        .name = "fake_touchbar_device_emit",
        .config = nullptr,
        .parent = nullptr,
    };
    static Device other_device {
        .name = "fake_touchbar_device_other",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);
    CHECK_EQ(device_construct_add(&other_device, "touchbar_test,fake"), ERROR_NONE);

    TouchbarEventSubscription sub_a {};
    TouchbarEventSubscription sub_b {};
    TouchbarEventSubscription sub_other {};
    CHECK_EQ(touchbar_subscribe(&fake_device, &sub_a), ERROR_NONE);
    CHECK_EQ(touchbar_subscribe(&fake_device, &sub_b), ERROR_NONE);
    CHECK_EQ(touchbar_subscribe(&other_device, &sub_other), ERROR_NONE);

    TouchbarEvent out {};
    CHECK_EQ(touchbar_poll(&fake_device, &sub_a, &out), ERROR_TIMEOUT);

    touchbar_emit_event(&fake_device, TouchbarEvent { .type = TOUCHBAR_EVENT_TOUCH_DOWN, .direction = 0, .channel = TEST_CHANNEL, .touch_mask = TEST_TOUCH_MASK, .position = TEST_POSITION });
    touchbar_emit_event(&fake_device, TouchbarEvent { .type = TOUCHBAR_EVENT_SWIPE, .direction = -1, .channel = 0, .touch_mask = 0, .position = 0 });

    for (auto* sub : { &sub_a, &sub_b }) {
        CHECK_EQ(touchbar_poll(&fake_device, sub, &out), ERROR_NONE);
        CHECK_EQ(out.type, TOUCHBAR_EVENT_TOUCH_DOWN);
        CHECK_EQ(out.channel, TEST_CHANNEL);
        CHECK_EQ(out.touch_mask, TEST_TOUCH_MASK);
        CHECK_EQ(out.position, TEST_POSITION);
        CHECK_EQ(touchbar_poll(&fake_device, sub, &out), ERROR_NONE);
        CHECK_EQ(out.type, TOUCHBAR_EVENT_SWIPE);
        CHECK_EQ(out.direction, -1);
        CHECK_EQ(touchbar_poll(&fake_device, sub, &out), ERROR_TIMEOUT);
    }
    CHECK_EQ(touchbar_poll(&other_device, &sub_other, &out), ERROR_TIMEOUT);

    CHECK_EQ(touchbar_unsubscribe(&fake_device, &sub_a), ERROR_NONE);
    CHECK_EQ(touchbar_unsubscribe(&fake_device, &sub_b), ERROR_NONE);
    CHECK_EQ(touchbar_unsubscribe(&other_device, &sub_other), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_remove(&other_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&other_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_emit_event drops the newest non-move events of a full queue") {
    static Device fake_device {
        .name = "fake_touchbar_device_full",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);

    TouchbarEventSubscription sub {};
    CHECK_EQ(touchbar_subscribe(&fake_device, &sub), ERROR_NONE);

    for (uint16_t i = 0; i < TOUCHBAR_EVENT_QUEUE_CAPACITY + 3; i++) {
        touchbar_emit_event(&fake_device, make_event(TOUCHBAR_EVENT_TAP, i));
    }

    TouchbarEvent out {};
    for (uint16_t i = 0; i < TOUCHBAR_EVENT_QUEUE_CAPACITY; i++) {
        CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_NONE);
        CHECK_EQ(out.position, i);
    }
    CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_TIMEOUT);

    CHECK_EQ(touchbar_unsubscribe(&fake_device, &sub), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_subscribe rejects a subscription that is already registered, and unsubscribe reports an unknown one") {
    static Device fake_device {
        .name = "fake_touchbar_device_subscribe",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);

    TouchbarEventSubscription sub {};
    CHECK_EQ(touchbar_subscribe(&fake_device, &sub), ERROR_NONE);
    CHECK_EQ(touchbar_subscribe(&fake_device, &sub), ERROR_INVALID_STATE);
    CHECK_EQ(touchbar_unsubscribe(&fake_device, &sub), ERROR_NONE);
    CHECK_EQ(touchbar_unsubscribe(&fake_device, &sub), ERROR_NOT_FOUND);

    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_emit_event merges consecutive moves into the newest one and keeps the events around them") {
    static Device fake_device {
        .name = "fake_touchbar_device_move",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);

    TouchbarEventSubscription sub {};
    CHECK_EQ(touchbar_subscribe(&fake_device, &sub), ERROR_NONE);

    // More moves than the queue can hold, so merging is the only way that the touch up still fits
    touchbar_emit_event(&fake_device, make_event(TOUCHBAR_EVENT_TOUCH_DOWN, 1));
    for (uint16_t i = 0; i < TOUCHBAR_EVENT_QUEUE_CAPACITY * 2; i++) {
        touchbar_emit_event(&fake_device, make_event(TOUCHBAR_EVENT_MOVE, 100 + i));
    }
    touchbar_emit_event(&fake_device, make_event(TOUCHBAR_EVENT_TOUCH_UP, 2));
    touchbar_emit_event(&fake_device, make_event(TOUCHBAR_EVENT_MOVE, 3));

    TouchbarEvent out {};
    CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_NONE);
    CHECK_EQ(out.type, TOUCHBAR_EVENT_TOUCH_DOWN);
    CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_NONE);
    CHECK_EQ(out.type, TOUCHBAR_EVENT_MOVE);
    CHECK_EQ(out.position, 100 + TOUCHBAR_EVENT_QUEUE_CAPACITY * 2 - 1);
    CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_NONE);
    CHECK_EQ(out.type, TOUCHBAR_EVENT_TOUCH_UP);
    CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_NONE);
    CHECK_EQ(out.type, TOUCHBAR_EVENT_MOVE);
    CHECK_EQ(out.position, 3);
    CHECK_EQ(touchbar_poll(&fake_device, &sub, &out), ERROR_TIMEOUT);

    CHECK_EQ(touchbar_unsubscribe(&fake_device, &sub), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_has_capability requires every queried capability") {
    static Device fake_device {
        .name = "fake_touchbar_device_capability",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);

    CHECK(touchbar_has_capability(&fake_device, TOUCHBAR_CAPABILITY_POSITION));
    CHECK(touchbar_has_capability(&fake_device, TOUCHBAR_CAPABILITY_POSITION | TOUCHBAR_CAPABILITY_GESTURE_SWIPE));
    CHECK_FALSE(touchbar_has_capability(&fake_device, TOUCHBAR_CAPABILITY_GESTURE_FLICK));
    CHECK_FALSE(touchbar_has_capability(&fake_device, TOUCHBAR_CAPABILITY_POSITION | TOUCHBAR_CAPABILITY_GESTURE_FLICK));

    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_recalibrate forwards to the driver, reports ERROR_NOT_SUPPORTED without one and ERROR_INVALID_STATE when not started") {
    recalibrate_call_count = 0;

    static Device fake_device {
        .name = "fake_touchbar_device_recalibrate",
        .config = nullptr,
        .parent = nullptr,
    };
    static Device minimal_device {
        .name = "fake_touchbar_device_minimal",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(driver_construct_add(&minimal_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&fake_device, "touchbar_test,fake"), ERROR_NONE);
    CHECK_EQ(device_construct_add(&minimal_device, "touchbar_test,minimal"), ERROR_NONE);

    CHECK_EQ(touchbar_recalibrate(&fake_device), ERROR_INVALID_STATE);
    CHECK_EQ(recalibrate_call_count, 0);

    CHECK_EQ(device_start(&fake_device), ERROR_NONE);
    CHECK_EQ(device_start(&minimal_device), ERROR_NONE);

    CHECK_EQ(touchbar_recalibrate(&fake_device), ERROR_NONE);
    CHECK_EQ(recalibrate_call_count, 1);
    CHECK_EQ(touchbar_recalibrate(&minimal_device), ERROR_NOT_SUPPORTED);
    CHECK_EQ(recalibrate_call_count, 1);
    CHECK_FALSE(touchbar_has_capability(&minimal_device, TOUCHBAR_CAPABILITY_POSITION));

    CHECK_EQ(device_stop(&fake_device), ERROR_NONE);
    CHECK_EQ(device_stop(&minimal_device), ERROR_NONE);
    CHECK_EQ(device_remove(&fake_device), ERROR_NONE);
    CHECK_EQ(device_remove(&minimal_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&fake_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&minimal_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&fake_touchbar_driver), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&minimal_touchbar_driver), ERROR_NONE);
}

TEST_CASE("touchbar_has_capability prefers the driver's has_capability over the static capabilities") {
    static Device dynamic_device {
        .name = "fake_touchbar_device_dynamic",
        .config = nullptr,
        .parent = nullptr,
    };

    CHECK_EQ(driver_construct_add(&dynamic_touchbar_driver), ERROR_NONE);
    CHECK_EQ(device_construct_add(&dynamic_device, "touchbar_test,dynamic"), ERROR_NONE);

    CHECK(touchbar_has_capability(&dynamic_device, TOUCHBAR_CAPABILITY_GESTURE_HOLD));
    CHECK_FALSE(touchbar_has_capability(&dynamic_device, TOUCHBAR_CAPABILITY_POSITION));

    CHECK_EQ(device_remove(&dynamic_device), ERROR_NONE);
    CHECK_EQ(device_destruct(&dynamic_device), ERROR_NONE);
    CHECK_EQ(driver_remove_destruct(&dynamic_touchbar_driver), ERROR_NONE);
}
