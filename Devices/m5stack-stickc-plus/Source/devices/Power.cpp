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
        // Reference: https://github.com/pr3y/Bruce/blob/main/lib/utility/AXP192.cpp
        axp192_ioctl(driver, AXP192_LDO23_VOLTAGE, 3300); // LCD backlight
        axp192_ioctl(driver, AXP192_ADC_ENABLE_1, 0xff); // Set all ADC enabled
        axp192_ioctl(driver, AXP192_CHARGE_CONTROL_1, 0xc0); // Battery charging at 4.2 V and 100 mA
        axp192_ioctl(driver, AXP192_ADC_ENABLE_1, 0xff); // Enable battery, AC in, Vbus, APS ADC
        uint8_t buffer;
        axp192_read(driver, AXP192_DCDC13_LDO23_CONTROL, &buffer);
        axp192_ioctl(driver, AXP192_DCDC13_LDO23_CONTROL, buffer | 0x4D); // Enable Ext, LDO2, LDO3, DCDC1
        axp192_ioctl(driver, AXP192_PEK, 0x0C); // 128 ms power on, 4 s power off
        axp192_ioctl(driver, AXP192_GPIO0_LDOIO0_VOLTAGE, 0xF0); // 3.3 V RTC voltage
        axp192_ioctl(driver, AXP192_GPIO0_CONTROL, 0x02); // Set GPIO0 to LDO
        axp192_ioctl(driver, AXP192_VBUS_IPSOUT_CHANNEL, 0x80); // Disable Vbus hold limit
        axp192_ioctl(driver, AXP192_BATTERY_CHARGE_HIGH_TEMP, 0xFC); // Set temperature protection
        axp192_ioctl(driver, AXP192_BATTERY_CHARGE_CONTROL, 0xA2); // Enable RTC BAT charge
        axp192_ioctl(driver, AXP192_SHUTDOWN_BATTERY_CHGLED_CONTROL, 0x46); // Enable bat detection
        return true;
    });

}
