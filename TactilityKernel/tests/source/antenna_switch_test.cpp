#include "doctest.h"

#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/antenna_switch.h>
#include <tactility/drivers/gpio_antenna_switch.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/gpio_descriptor.h>

extern "C" Driver gpio_antenna_switch_driver;

namespace {

struct FakeGpioController {
    bool levels[4] {};
};

error_t fake_gpio_set_level(GpioDescriptor* descriptor, bool high) {
    auto* context = static_cast<FakeGpioController*>(gpio_controller_get_controller_context(descriptor->controller));
    context->levels[descriptor->pin] = high;
    return ERROR_NONE;
}

error_t fake_gpio_get_level(GpioDescriptor* descriptor, bool* high) {
    auto* context = static_cast<FakeGpioController*>(gpio_controller_get_controller_context(descriptor->controller));
    *high = context->levels[descriptor->pin];
    return ERROR_NONE;
}

error_t fake_gpio_set_flags(GpioDescriptor*, gpio_flags_t) {
    return ERROR_NONE;
}

error_t fake_gpio_get_flags(GpioDescriptor* descriptor, gpio_flags_t* flags) {
    *flags = descriptor->flags;
    return ERROR_NONE;
}

error_t fake_gpio_get_native_pin_number(GpioDescriptor*, void*) {
    return ERROR_NOT_SUPPORTED;
}

const GpioControllerApi fake_gpio_api = {
    .set_level = fake_gpio_set_level,
    .get_level = fake_gpio_get_level,
    .set_flags = fake_gpio_set_flags,
    .get_flags = fake_gpio_get_flags,
    .get_native_pin_number = fake_gpio_get_native_pin_number,
};

error_t fake_gpio_start(Device* device) {
    auto* context = const_cast<FakeGpioController*>(static_cast<const FakeGpioController*>(device->config));
    return gpio_controller_init_descriptors(device, 4, context);
}

error_t fake_gpio_stop(Device* device) {
    return gpio_controller_deinit_descriptors(device);
}

Driver fake_gpio_driver = {
    .name = "antenna_switch_test_gpio",
    .compatible = (const char*[]) { "antenna_switch_test,gpio", nullptr },
    .start_device = fake_gpio_start,
    .stop_device = fake_gpio_stop,
    .probe = nullptr,
    .api = &fake_gpio_api,
    .device_type = &GPIO_CONTROLLER_TYPE,
    .internal = nullptr
};

struct AntennaSwitchFixture {
    FakeGpioController gpio_context {};
    Device gpio_device {
        .name = "antenna_switch_test_gpio",
        .config = &gpio_context,
        .parent = nullptr,
    };
    GpioPinSpec gpios[2] {
        { &gpio_device, 0, GPIO_FLAG_NONE },
        { &gpio_device, 1, GPIO_FLAG_NONE },
    };
    GpioAntennaSwitchConfig switch_config {
        .gpios = gpios,
        .gpios_count = 2,
        .default_antenna = 1,
    };
    Device switch_device {
        .name = "antenna_switch_test_switch",
        .config = &switch_config,
        .parent = nullptr,
    };
    bool gpio_driver_added = false;
    bool gpio_device_added = false;
    bool gpio_device_started = false;
    bool switch_device_added = false;
    bool switch_device_started = false;

    error_t start() {
        error_t error = driver_construct_add(&fake_gpio_driver);
        if (error != ERROR_NONE) return error;
        gpio_driver_added = true;

        error = device_construct_add(&gpio_device, "antenna_switch_test,gpio");
        if (error != ERROR_NONE) return error;
        gpio_device_added = true;

        error = device_start(&gpio_device);
        if (error != ERROR_NONE) return error;
        gpio_device_started = true;

        error = device_construct_add(&switch_device, "tactility,gpio-antenna-switch");
        if (error != ERROR_NONE) return error;
        switch_device_added = true;

        error = device_start(&switch_device);
        if (error == ERROR_NONE) switch_device_started = true;
        return error;
    }

    ~AntennaSwitchFixture() {
        if (switch_device_started) device_stop(&switch_device);
        if (switch_device_added) {
            device_remove(&switch_device);
            device_destruct(&switch_device);
        }
        if (gpio_device_started) device_stop(&gpio_device);
        if (gpio_device_added) {
            device_remove(&gpio_device);
            device_destruct(&gpio_device);
        }
        if (gpio_driver_added) driver_remove_destruct(&fake_gpio_driver);
    }
};

} // namespace

TEST_CASE("GPIO antenna switch selects and reports binary antenna indices") {
    AntennaSwitchFixture fixture;
    REQUIRE_EQ(fixture.start(), ERROR_NONE);

    CHECK(fixture.gpio_context.levels[0]);
    CHECK_FALSE(fixture.gpio_context.levels[1]);

    uint32_t antenna = 0;
    CHECK_EQ(antenna_switch_get_antenna(&fixture.switch_device, &antenna), ERROR_NONE);
    CHECK_EQ(antenna, 1);

    CHECK_EQ(antenna_switch_set_antenna(&fixture.switch_device, 2), ERROR_NONE);
    CHECK_FALSE(fixture.gpio_context.levels[0]);
    CHECK(fixture.gpio_context.levels[1]);
    CHECK_EQ(antenna_switch_get_antenna(&fixture.switch_device, &antenna), ERROR_NONE);
    CHECK_EQ(antenna, 2);

    CHECK_EQ(antenna_switch_set_antenna(&fixture.switch_device, 4), ERROR_OUT_OF_RANGE);
}
