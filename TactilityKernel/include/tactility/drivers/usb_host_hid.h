// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Device;
struct DeviceType;

typedef void* UsbHidQueueHandle;

/** Key values — numerically identical to lv_key_t so no translation is needed. */
typedef enum {
    USB_HID_KEY_UP        = 17,
    USB_HID_KEY_DOWN      = 18,
    USB_HID_KEY_RIGHT     = 19,
    USB_HID_KEY_LEFT      = 20,
    USB_HID_KEY_ESC       = 27,
    USB_HID_KEY_DEL       = 127,
    USB_HID_KEY_BACKSPACE = 8,
    USB_HID_KEY_ENTER     = 10,
    USB_HID_KEY_NEXT      = 9,
    USB_HID_KEY_PREV      = 11,
    USB_HID_KEY_HOME      = 2,
    USB_HID_KEY_END       = 3,
} UsbHidKey;

typedef enum {
    USB_HID_EVENT_KEY,
    USB_HID_EVENT_MOUSE_MOVE,
    USB_HID_EVENT_MOUSE_BTN,
    USB_HID_EVENT_SCROLL,
    USB_HID_EVENT_KEYBOARD_CONNECTED,
    USB_HID_EVENT_KEYBOARD_DISCONNECTED,
    USB_HID_EVENT_MOUSE_CONNECTED,
    USB_HID_EVENT_MOUSE_DISCONNECTED,
} UsbHidEventType;

typedef struct {
    UsbHidEventType type;
    union {
        struct { uint32_t key_code; bool pressed; } key;
        struct { int32_t dx; int32_t dy; }         mouse_move;
        struct { bool button1; bool button2; }     mouse_btn;
        struct { int32_t delta; }                  scroll;
    };
} UsbHidEvent;

struct UsbHidApi {
    bool (*is_connected)(struct Device* device);
    bool (*subscribe)(struct Device* device, UsbHidQueueHandle event_queue);
    void (*unsubscribe)(struct Device* device, UsbHidQueueHandle event_queue);
};

extern const struct DeviceType USB_HOST_HID_TYPE;

/** Find the first ready USB HID device. Returns NULL if unavailable. */
struct Device* usb_host_hid_get_device(void);

/** Returns true if any HID device (keyboard or mouse) is currently connected. */
bool usb_host_hid_is_connected(void);

/** Subscribe a FreeRTOS queue to receive UsbHidEvent items from the HID driver. */
bool usb_host_hid_subscribe(UsbHidQueueHandle event_queue);

/** Unsubscribe a previously subscribed queue. */
void usb_host_hid_unsubscribe(UsbHidQueueHandle event_queue);

#ifdef __cplusplus
}
#endif
