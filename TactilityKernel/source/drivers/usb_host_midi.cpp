#include <tactility/drivers/usb_host_midi.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define USB_MIDI_API(driver) ((struct UsbMidiApi*)(driver)->api)

extern "C" {

const struct DeviceType USB_HOST_MIDI_TYPE = {
    .name = "usb-host-midi",
};

static const struct UsbMidiApi* get_active_midi_api(struct Device** out_device) {
    struct Device* device = device_find_first_active_by_type(&USB_HOST_MIDI_TYPE);
    if (!device) return nullptr;
    struct Driver* drv = device_get_driver(device);
    if (!drv || !drv->api) return nullptr;
    if (out_device) *out_device = device;
    return USB_MIDI_API(drv);
}

bool usb_midi_set_callback(usb_midi_message_cb_t callback, void* user_data) {
    struct Device* device = nullptr;
    const struct UsbMidiApi* api = get_active_midi_api(&device);
    if (!api || !api->set_callback) return false;
    api->set_callback(device, callback, user_data);
    return true;
}

bool usb_midi_is_connected(void) {
    struct Device* device = nullptr;
    const struct UsbMidiApi* api = get_active_midi_api(&device);
    return api && api->is_connected && api->is_connected(device);
}

} // extern "C"
