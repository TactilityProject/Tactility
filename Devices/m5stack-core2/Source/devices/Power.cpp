#include <Axp192.h>
#include <tactility/device.h>

static std::shared_ptr<Axp192> axp192 = nullptr;

std::shared_ptr<Axp192> createAxp192() {
    assert(axp192 == nullptr);
    auto configuration = std::make_unique<Axp192::Configuration>(device_find_by_name("i2c_internal"));
    axp192 = std::make_shared<Axp192>(std::move(configuration));
    return axp192;
}

std::shared_ptr<Axp192> getAxp192() {
    assert(axp192 != nullptr);
    return axp192;
}

bool initAxp() {
    const auto axp = createAxp192();
    return axp->init([](auto* driver) {
        axp192_ioctl(driver, AXP192_LDO2_SET_VOLTAGE, 3300); // LCD + SD
        axp192_ioctl(driver, AXP192_LDO3_SET_VOLTAGE, 0); // VIB_MOTOR STOP
        axp192_ioctl(driver, AXP192_DCDC3_SET_VOLTAGE, 3300);

        axp192_ioctl(driver, AXP192_LDO2_ENABLE);
        axp192_ioctl(driver, AXP192_LDO3_DISABLE);
        axp192_ioctl(driver, AXP192_DCDC3_ENABLE);

        axp192_write(driver, AXP192_PWM1_DUTY_CYCLE_2, 255); // PWM 255 (LED OFF)
        axp192_write(driver, AXP192_GPIO1_CONTROL, 0x02); // GPIO1 PWM

        // TODO: We could charge at 390mA according to the M5Unified code, but the AXP driver in M5Unified limits to 132mA, so it's unclear what the AXP supports.
        return true;
    });

}
