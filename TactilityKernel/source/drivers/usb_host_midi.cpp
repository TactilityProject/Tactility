#include <tactility/drivers/usb_host_midi.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_MIDI_API(driver) ((struct UsbMidiApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_MIDI_TYPE = {
    .name = "usb-host-midi",
};

void usb_midi_set_callback(struct Device* device, usb_midi_message_cb_t callback, void* user_data) {
    auto* api = USB_MIDI_API(device_get_driver(device));
    api->set_callback(device, callback, user_data);
}

bool usb_midi_is_connected(struct Device* device) {
    auto* api = USB_MIDI_API(device_get_driver(device));
    return api->is_connected(device);
}

} // extern "C"
