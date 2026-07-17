// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/lora.h>
#include <tactility/device.h>
#include <tactility/driver.h>

#define LORA_API(device) ((const struct LoraApi*)device_get_driver(device)->api)

extern "C" {

struct Device* lora_find_first_registered_device() {
    struct Device* found = nullptr;
    device_for_each_of_type(&LORA_TYPE, &found, [](struct Device* dev, void* ctx) -> bool {
        *static_cast<struct Device**>(ctx) = dev;
        return false;
    });
    return found;
}

error_t lora_get_radio_state(struct Device* device, enum LoraRadioState* state) {
    return LORA_API(device)->get_radio_state(device, state);
}

error_t lora_set_enabled(struct Device* device, bool enabled) {
    return LORA_API(device)->set_enabled(device, enabled);
}

error_t lora_set_modulation(struct Device* device, enum LoraModulation modulation) {
    return LORA_API(device)->set_modulation(device, modulation);
}

error_t lora_get_modulation(struct Device* device, enum LoraModulation* modulation) {
    return LORA_API(device)->get_modulation(device, modulation);
}

bool lora_can_transmit(struct Device* device, enum LoraModulation modulation) {
    return LORA_API(device)->can_transmit(device, modulation);
}

bool lora_can_receive(struct Device* device, enum LoraModulation modulation) {
    return LORA_API(device)->can_receive(device, modulation);
}

error_t lora_set_parameter(struct Device* device, enum LoraParameter parameter, float value) {
    return LORA_API(device)->set_parameter(device, parameter, value);
}

error_t lora_get_parameter(struct Device* device, enum LoraParameter parameter, float* value) {
    return LORA_API(device)->get_parameter(device, parameter, value);
}

error_t lora_transmit(struct Device* device, const uint8_t* data, size_t length, LoraTxId* id) {
    return LORA_API(device)->transmit(device, data, length, id);
}

error_t lora_add_rx_callback(struct Device* device, void* callback_context, LoraRxCallback callback) {
    return LORA_API(device)->add_rx_callback(device, callback_context, callback);
}

error_t lora_remove_rx_callback(struct Device* device, LoraRxCallback callback) {
    return LORA_API(device)->remove_rx_callback(device, callback);
}

error_t lora_add_state_callback(struct Device* device, void* callback_context, LoraStateCallback callback) {
    return LORA_API(device)->add_state_callback(device, callback_context, callback);
}

error_t lora_remove_state_callback(struct Device* device, LoraStateCallback callback) {
    return LORA_API(device)->remove_state_callback(device, callback);
}

error_t lora_add_tx_callback(struct Device* device, void* callback_context, LoraTxCallback callback) {
    return LORA_API(device)->add_tx_callback(device, callback_context, callback);
}

error_t lora_remove_tx_callback(struct Device* device, LoraTxCallback callback) {
    return LORA_API(device)->remove_tx_callback(device, callback);
}

const struct DeviceType LORA_TYPE = {
    .name = "lora"
};

} // extern "C"
