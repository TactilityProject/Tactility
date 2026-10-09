#include "doctest.h"

#include <tactility/concurrent/task_event_group.h>
#include <tactility/device.h>
#include <tactility/drivers/usb_host_midi.h>

static usb_midi_message_t make_message(uint8_t data1) {
    return usb_midi_message_t { .cable = 0, .status = 0x90, .data1 = data1, .data2 = 100 };
}

TEST_CASE("usb_midi_event_emit queues messages in FIFO order and signals the subscription bit") {
    Device device = {};
    TaskEventGroup group;
    task_event_group_construct(&group);
    UsbMidiEventSubscription sub = {};

    REQUIRE_EQ(usb_midi_event_subscribe(&sub, &group, &device), ERROR_NONE);

    auto first = make_message(60);
    auto second = make_message(64);
    CHECK_EQ(usb_midi_event_emit(&device, &first), ERROR_NONE);
    CHECK_EQ(usb_midi_event_emit(&device, &second), ERROR_NONE);

    uint32_t flags = 0;
    CHECK_EQ(task_event_group_wait(&group, sub.bit, false, &flags, 0), ERROR_NONE);
    CHECK_EQ(flags, sub.bit);

    UsbMidiEvent event = {};
    REQUIRE_EQ(usb_midi_event_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.message.data1, 60);
    CHECK_EQ(event.message.status, 0x90);
    REQUIRE_EQ(usb_midi_event_poll(&sub, &event), ERROR_NONE);
    CHECK_EQ(event.message.data1, 64);
    CHECK_EQ(usb_midi_event_poll(&sub, &event), ERROR_TIMEOUT);

    CHECK_EQ(usb_midi_event_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("usb_midi_event_emit only delivers to subscriptions of the emitting device") {
    Device device_a = {};
    Device device_b = {};
    TaskEventGroup group;
    task_event_group_construct(&group);
    UsbMidiEventSubscription sub = {};

    REQUIRE_EQ(usb_midi_event_subscribe(&sub, &group, &device_a), ERROR_NONE);

    auto message = make_message(60);
    CHECK_EQ(usb_midi_event_emit(&device_b, &message), ERROR_NOT_FOUND);

    UsbMidiEvent event = {};
    CHECK_EQ(usb_midi_event_poll(&sub, &event), ERROR_TIMEOUT);

    CHECK_EQ(usb_midi_event_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("usb_midi_event_emit drops the newest message when the queue is full") {
    Device device = {};
    TaskEventGroup group;
    task_event_group_construct(&group);
    UsbMidiEventSubscription sub = {};

    REQUIRE_EQ(usb_midi_event_subscribe(&sub, &group, &device), ERROR_NONE);

    for (int i = 0; i < USB_MIDI_EVENT_QUEUE_CAPACITY; i++) {
        auto message = make_message(static_cast<uint8_t>(i));
        CHECK_EQ(usb_midi_event_emit(&device, &message), ERROR_NONE);
    }
    auto overflow = make_message(127);
    CHECK_EQ(usb_midi_event_emit(&device, &overflow), ERROR_RESOURCE);

    UsbMidiEvent event = {};
    for (int i = 0; i < USB_MIDI_EVENT_QUEUE_CAPACITY; i++) {
        REQUIRE_EQ(usb_midi_event_poll(&sub, &event), ERROR_NONE);
        CHECK_EQ(event.message.data1, i);
    }
    CHECK_EQ(usb_midi_event_poll(&sub, &event), ERROR_TIMEOUT);

    CHECK_EQ(usb_midi_event_unsubscribe(&sub), ERROR_NONE);
    task_event_group_destruct(&group);
}

TEST_CASE("usb_midi_event_subscribe rejects a subscription that is already registered") {
    Device device = {};
    TaskEventGroup group;
    task_event_group_construct(&group);
    UsbMidiEventSubscription sub = {};

    REQUIRE_EQ(usb_midi_event_subscribe(&sub, &group, &device), ERROR_NONE);
    CHECK_EQ(usb_midi_event_subscribe(&sub, &group, &device), ERROR_INVALID_STATE);
    CHECK_EQ(usb_midi_event_unsubscribe(&sub), ERROR_NONE);
    CHECK_EQ(usb_midi_event_unsubscribe(&sub), ERROR_NOT_FOUND);

    task_event_group_destruct(&group);
}
