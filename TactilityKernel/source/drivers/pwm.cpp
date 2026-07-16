// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/pwm.h>
#include <tactility/device.h>

#define PWM_DRIVER_API(driver) ((struct PwmApi*)driver->api)

extern "C" {

error_t pwm_set_period(Device* device, uint32_t period_ns) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->set_period(device, period_ns);
}

error_t pwm_get_period(Device* device, uint32_t* period_ns) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->get_period(device, period_ns);
}

error_t pwm_set_duty(Device* device, uint32_t duty_ns) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->set_duty(device, duty_ns);
}

error_t pwm_get_duty(Device* device, uint32_t* duty_ns) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->get_duty(device, duty_ns);
}

error_t pwm_set_inverted(Device* device, bool inverted) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->set_inverted(device, inverted);
}

error_t pwm_is_inverted(Device* device, bool* inverted) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->is_inverted(device, inverted);
}

error_t pwm_enable(Device* device) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->enable(device);
}

error_t pwm_disable(Device* device) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->disable(device);
}

error_t pwm_is_enabled(Device* device, bool* enabled) {
    const auto* driver = device_get_driver(device);
    return PWM_DRIVER_API(driver)->is_enabled(device, enabled);
}

const DeviceType PWM_TYPE {
    .name = "pwm"
};

}
