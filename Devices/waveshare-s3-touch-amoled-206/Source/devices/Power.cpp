#include "Power.h"
#include <Axp2101.h>
#include <Tactility/Logger.h>

static const auto LOGGER = tt::Logger("WavesharePMU");

std::shared_ptr<Axp2101> axp2101;

bool initAxp() {
    axp2101 = std::make_shared<Axp2101>(I2C_NUM_0);

    static constexpr uint8_t reg_data_array[] = {
        0x90U, 0xFFU,  // Enable all LDO outputs
        0x92U, 18U - 5U,  // ALDO1 = 1.8V
        0x93U, 33U - 5U,  // ALDO2 = 3.3V
        0x94U, 33U - 5U,  // ALDO3 = 3.3V
        0x95U, 33U - 5U,  // ALDO4 = 3.3V
        0x27, 0x00,       // PowerKey: Hold=1sec, PowerOff=4sec
        0x69, 0x11,       // CHGLED function
        0x10, 0x30,       // PMU common config
        0x30, 0x0F,       // ADC enabled (for voltage measurement)
        0x31, 0x0C,       // ADC for battery voltage and current
        0x32, 0x46,       // ADC for temperature and VBUS
        // NOTE: Fuel gauge (0xA2) and battery parameter (0xA1) are NOT
        // configured here. The AXP2101 E-gauge requires a 128-byte battery
        // profile specific to the cell. Without it the gauge sticks at 100%.
        // Charge level is instead estimated from battery voltage, which is
        // accurate for the standard 3.7 V / 400 mAh Li-Po on this board.
    };

    if (axp2101->setRegisters((uint8_t*)reg_data_array, sizeof(reg_data_array))) {
        LOGGER.info("AXP2101 initialized for 400mAh Li-Po voltage monitoring");
        return true;
    } else {
        LOGGER.error("AXP2101: Failed to initialize");
        return false;
    }
}

std::shared_ptr<Axp2101> getAxp2101() {
    return axp2101;
}