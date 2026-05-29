#include <tactility/drivers/usb_host_midi.h>
#include <tactility/device.h>
#include <tactility/driver.h>

static const struct UsbMidiApi* get_midi_api(struct Device* device) {
    if (!device) return nullptr;
    struct Driver* drv = device_get_driver(device);
    if (!drv || !drv->api) return nullptr;
    return static_cast<const struct UsbMidiApi*>(drv->api);
}

extern "C" {

const struct DeviceType USB_HOST_MIDI_TYPE = {
    .name = "usb-host-midi",
};

struct Device* usb_host_midi_get_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&USB_HOST_MIDI_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

bool usb_midi_set_callback(usb_midi_message_cb_t callback, void* user_data) {
    struct Device* device = usb_host_midi_get_device();
    const struct UsbMidiApi* api = get_midi_api(device);
    if (!api || !api->set_callback) return false;
    api->set_callback(device, callback, user_data);
    return true;
}

bool usb_midi_is_connected(void) {
    struct Device* device = usb_host_midi_get_device();
    const struct UsbMidiApi* api = get_midi_api(device);
    return api && api->is_connected(device);
}

} // extern "C"
