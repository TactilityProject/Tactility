#include <tactility/drivers/bluetooth_midi.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define BT_MIDI_API(device) ((const struct BtMidiApi*)device_get_driver(device)->api)

extern "C" {

const struct DeviceType BLUETOOTH_MIDI_TYPE = {
    .name = "bluetooth-midi",
};

struct Device* bluetooth_midi_get_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&BLUETOOTH_MIDI_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        if (device_is_ready(dev)) {
            *static_cast<struct Device**>(ctx) = dev;
            return false;
        }
        return true;
    });
    return found;
}

error_t bluetooth_midi_start(struct Device* device) {
    return BT_MIDI_API(device)->start(device);
}

error_t bluetooth_midi_stop(struct Device* device) {
    return BT_MIDI_API(device)->stop(device);
}

error_t bluetooth_midi_send(struct Device* device, const uint8_t* msg, size_t len) {
    return BT_MIDI_API(device)->send(device, msg, len);
}

bool bluetooth_midi_is_connected(struct Device* device) {
    return BT_MIDI_API(device)->is_connected(device);
}

} // extern "C"
