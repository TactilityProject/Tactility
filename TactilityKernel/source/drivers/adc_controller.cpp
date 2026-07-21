// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/adc_controller.h>
#include <tactility/device.h>

#define ADC_CONTROLLER_DRIVER_API(driver) ((struct AdcControllerApi*)driver->api)

extern "C" {

error_t adc_controller_read_raw(Device* device, uint8_t channel, int* out_raw, TickType_t timeout) {
    const auto* driver = device_get_driver(device);
    return ADC_CONTROLLER_DRIVER_API(driver)->read_raw(device, channel, out_raw, timeout);
}

error_t adc_channel_read_raw(const struct AdcChannelSpec* spec, int* out_raw, TickType_t timeout) {
    return adc_controller_read_raw(spec->adc_controller, spec->channel, out_raw, timeout);
}

const struct DeviceType ADC_CONTROLLER_TYPE {
    .name = "adc-controller"
};

}
