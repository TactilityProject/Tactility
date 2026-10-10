#include <tactility/drivers/usb_host_midi.h>
#include <tactility/concurrent/mutex.h>
#include <tactility/device.h>
#include <tactility/time.h>

struct UsbMidiEventMutex {
    Mutex handle {};
    UsbMidiEventMutex() { mutex_construct(&handle); }
    ~UsbMidiEventMutex() { mutex_destruct(&handle); }
};

static UsbMidiEventSubscription* usb_midi_event_subscriptions = nullptr;
static UsbMidiEventMutex usb_midi_event_subscriptions_mutex;

extern "C" {

const struct DeviceType USB_HOST_MIDI_TYPE = {
    .name = "usb-host-midi",
};

error_t usb_midi_event_subscribe(UsbMidiEventSubscription* sub, TaskEventGroup* event_group, Device* device) {
    uint32_t bit;
    error_t claim_result = task_event_group_claim_bit(event_group, &bit);
    if (claim_result != ERROR_NONE) {
        return claim_result;
    }

    mutex_lock(&usb_midi_event_subscriptions_mutex.handle);

    // Avoid cyclic subscription list that would loop forever
    for (UsbMidiEventSubscription* existing = usb_midi_event_subscriptions; existing != nullptr; existing = existing->internal.next) {
        if (existing == sub) {
            mutex_unlock(&usb_midi_event_subscriptions_mutex.handle);
            task_event_group_release_bit(event_group, bit);
            return ERROR_INVALID_STATE;
        }
    }

    sub->bit = bit;
    sub->internal.device = device;
    sub->internal.event_group = event_group;
    sub->internal.head = 0;
    sub->internal.count = 0;
    sub->internal.next = usb_midi_event_subscriptions;
    usb_midi_event_subscriptions = sub;
    mutex_unlock(&usb_midi_event_subscriptions_mutex.handle);

    return ERROR_NONE;
}

error_t usb_midi_event_unsubscribe(UsbMidiEventSubscription* sub) {
    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&usb_midi_event_subscriptions_mutex.handle);
    for (UsbMidiEventSubscription** link = &usb_midi_event_subscriptions; *link != nullptr; link = &(*link)->internal.next) {
        if (*link == sub) {
            *link = sub->internal.next;
            result = ERROR_NONE;
            break;
        }
    }
    mutex_unlock(&usb_midi_event_subscriptions_mutex.handle);

    if (result == ERROR_NONE) {
        task_event_group_release_bit(sub->internal.event_group, sub->bit);
    }

    return result;
}

error_t usb_midi_event_emit(Device* device, const usb_midi_message_t* message) {
    UsbMidiEvent stamped_event {};
    stamped_event.timestamp = get_micros_since_boot();
    stamped_event.message = *message;

    error_t result = ERROR_NOT_FOUND;

    mutex_lock(&usb_midi_event_subscriptions_mutex.handle);
    for (UsbMidiEventSubscription* sub = usb_midi_event_subscriptions; sub != nullptr; sub = sub->internal.next) {
        if (sub->internal.device != device) {
            continue;
        }

        if (sub->internal.count >= USB_MIDI_EVENT_QUEUE_CAPACITY) {
            result = ERROR_RESOURCE;
            continue;
        }

        uint8_t tail = (sub->internal.head + sub->internal.count) % USB_MIDI_EVENT_QUEUE_CAPACITY;
        sub->internal.queue[tail] = stamped_event;
        sub->internal.count++;
        if (result != ERROR_RESOURCE) {
            result = ERROR_NONE;
        }
        task_event_group_signal(sub->internal.event_group, sub->bit);
    }
    mutex_unlock(&usb_midi_event_subscriptions_mutex.handle);

    return result;
}

error_t usb_midi_event_poll(UsbMidiEventSubscription* sub, UsbMidiEvent* out_event) {
    mutex_lock(&usb_midi_event_subscriptions_mutex.handle);
    bool has_event = sub->internal.count > 0;
    if (has_event) {
        *out_event = sub->internal.queue[sub->internal.head];
        sub->internal.head = (sub->internal.head + 1) % USB_MIDI_EVENT_QUEUE_CAPACITY;
        sub->internal.count--;
    }
    mutex_unlock(&usb_midi_event_subscriptions_mutex.handle);
    return has_event ? ERROR_NONE : ERROR_TIMEOUT;
}

} // extern "C"
