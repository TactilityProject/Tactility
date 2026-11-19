#include "doctest.h"

#include <tactility/drivers/button.h>

namespace {

ButtonEvent make_event(uint16_t code, ButtonValue value) {
    return ButtonEvent { .code = code, .value = value, .timestamp_ms = 0 };
}

}

TEST_CASE("button_poll returns events in the order they were emitted") {
    auto* device = reinterpret_cast<Device*>(0x1000);
    ButtonEventSubscription sub;
    REQUIRE_EQ(button_subscribe(device, &sub), ERROR_NONE);

    button_emit_event(device, make_event(BUTTON_KEY_ENTER, BUTTON_VALUE_PRESSED));
    button_emit_event(device, make_event(BUTTON_KEY_ENTER, BUTTON_VALUE_RELEASED));

    ButtonEvent event;
    REQUIRE_EQ(button_poll(device, &sub, &event), ERROR_NONE);
    CHECK_EQ(event.code, BUTTON_KEY_ENTER);
    CHECK_EQ(event.value, BUTTON_VALUE_PRESSED);
    REQUIRE_EQ(button_poll(device, &sub, &event), ERROR_NONE);
    CHECK_EQ(event.value, BUTTON_VALUE_RELEASED);
    CHECK_EQ(button_poll(device, &sub, &event), ERROR_TIMEOUT);

    CHECK_EQ(button_unsubscribe(device, &sub), ERROR_NONE);
}

TEST_CASE("button events only reach subscribers of the emitting device") {
    auto* device_a = reinterpret_cast<Device*>(0x1000);
    auto* device_b = reinterpret_cast<Device*>(0x2000);
    ButtonEventSubscription sub;
    REQUIRE_EQ(button_subscribe(device_a, &sub), ERROR_NONE);

    button_emit_event(device_b, make_event(BUTTON_KEY_ESC, BUTTON_VALUE_PRESSED));

    ButtonEvent event;
    CHECK_EQ(button_poll(device_a, &sub, &event), ERROR_TIMEOUT);
    CHECK_EQ(button_unsubscribe(device_a, &sub), ERROR_NONE);
}

TEST_CASE("button queue drops events beyond its capacity and keeps the oldest") {
    auto* device = reinterpret_cast<Device*>(0x1000);
    ButtonEventSubscription sub;
    REQUIRE_EQ(button_subscribe(device, &sub), ERROR_NONE);

    for (int i = 0; i < BUTTON_EVENT_QUEUE_CAPACITY + 2; i++) {
        button_emit_event(device, make_event(static_cast<uint16_t>(i + 1), BUTTON_VALUE_PRESSED));
    }

    ButtonEvent event;
    for (int i = 0; i < BUTTON_EVENT_QUEUE_CAPACITY; i++) {
        REQUIRE_EQ(button_poll(device, &sub, &event), ERROR_NONE);
        CHECK_EQ(event.code, i + 1);
    }
    CHECK_EQ(button_poll(device, &sub, &event), ERROR_TIMEOUT);
    CHECK_EQ(button_unsubscribe(device, &sub), ERROR_NONE);
}

TEST_CASE("button_subscribe rejects a double subscription and unsubscribe of an unknown one") {
    auto* device = reinterpret_cast<Device*>(0x1000);
    ButtonEventSubscription sub;
    ButtonEventSubscription other;
    REQUIRE_EQ(button_subscribe(device, &sub), ERROR_NONE);
    CHECK_EQ(button_subscribe(device, &sub), ERROR_INVALID_STATE);
    CHECK_EQ(button_unsubscribe(device, &other), ERROR_NOT_FOUND);
    CHECK_EQ(button_unsubscribe(device, &sub), ERROR_NONE);
}

namespace {

struct CallbackLog {
    int count = 0;
    uint16_t codes[BUTTON_CALLBACK_CAPACITY + 2] = {};
    Device* device = nullptr;
};

void log_event(Device* device, const ButtonEvent* event, void* context) {
    auto* log = static_cast<CallbackLog*>(context);
    log->device = device;
    log->codes[log->count++] = event->code;
}

void remove_self(Device*, const ButtonEvent*, void* context) {
    auto* count = static_cast<int*>(context);
    (*count)++;
    button_callback_remove(remove_self);
}

void ignore(Device*, const ButtonEvent*, void*) {}

}

TEST_CASE("button callbacks receive events in order until they are removed") {
    auto* device = reinterpret_cast<Device*>(0x1000);
    CallbackLog log;
    REQUIRE_EQ(button_callback_add(log_event, &log), ERROR_NONE);

    button_emit_event(device, make_event(BUTTON_KEY_POWER, BUTTON_VALUE_PRESSED));
    button_emit_event(device, make_event(BUTTON_KEY_STANDBY, BUTTON_VALUE_PRESSED));
    CHECK_EQ(log.count, 2);
    CHECK_EQ(log.codes[0], BUTTON_KEY_POWER);
    CHECK_EQ(log.codes[1], BUTTON_KEY_STANDBY);
    CHECK_EQ(log.device, device);

    CHECK_EQ(button_callback_remove(log_event), ERROR_NONE);
    button_emit_event(device, make_event(BUTTON_KEY_ENTER, BUTTON_VALUE_PRESSED));
    CHECK_EQ(log.count, 2);
    CHECK_EQ(button_callback_remove(log_event), ERROR_NOT_FOUND);
}

TEST_CASE("a button callback can remove itself while it runs") {
    auto* device = reinterpret_cast<Device*>(0x1000);
    int count = 0;
    REQUIRE_EQ(button_callback_add(remove_self, &count), ERROR_NONE);

    button_emit_event(device, make_event(BUTTON_KEY_POWER, BUTTON_VALUE_PRESSED));
    button_emit_event(device, make_event(BUTTON_KEY_POWER, BUTTON_VALUE_PRESSED));
    CHECK_EQ(count, 1);
}

TEST_CASE("button_callback_add fails when the registry is full") {
    for (int i = 0; i < BUTTON_CALLBACK_CAPACITY; i++) {
        REQUIRE_EQ(button_callback_add(ignore, nullptr), ERROR_NONE);
    }
    CHECK_EQ(button_callback_add(ignore, nullptr), ERROR_RESOURCE);
    for (int i = 0; i < BUTTON_CALLBACK_CAPACITY; i++) {
        CHECK_EQ(button_callback_remove(ignore), ERROR_NONE);
    }
}
