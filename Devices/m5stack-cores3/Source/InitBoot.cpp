#include "InitBoot.h"

#include <Tactility/Logger.h>
#include <Tactility/kernel/Kernel.h>

static const auto LOGGER = tt::Logger("CoreS3");

std::shared_ptr<Axp2101> axp2101;
std::shared_ptr<Aw9523> aw9523;

/**
 * For details see https://github.com/espressif/esp-bsp/blob/master/bsp/m5stack_core_s3/m5stack_core_s3.c
 * and schematic: https://m5stack.oss-cn-shenzhen.aliyuncs.com/resource/docs/datasheet/core/K128%20CoreS3/Sch_M5_CoreS3_v1.0.pdf
 */
bool initGpioExpander() {
    LOGGER.info("AW9523 init");

    /**
     * P0 pins:
     * 0: Touch reset
     * 1: Bus out enable
     * 2: AW88298 reset (I2S)
     * 3: ES7210 interrupt (Audio ADC)
     * 4: SD Card SW(itch on?)
     * 5: USB OTG enable
     * 6: /
     * 7: /
     */

    /**
     * P1 pins:
     * 0: Cam reset
     * 1: LCD reset
     * 2: Touch interrupt
     * 3: AW88298 interrupt (I2S)
     * 4: /
     * 5: /
     * 6: /
     * 7: Boost enable
     */

    uint8_t p0_state = 0U;
    uint8_t p1_state = 0U;

    // Enable touch
    p0_state |= (1U);
    // Bus out enable
    p0_state |= (1U << 1U);
    // I2S
    p0_state |= (1U << 2U);
    // SD card
    p0_state |= (1U << 4U);

    // Enable LCD
    p1_state |= (1U << 1U);
    // Boost enable
    p1_state |= (1U << 7U);

    /* AW9523 P0 is in push-pull mode */
    if (!aw9523->writeCTL(0x10)) {
        LOGGER.error("AW9523: Failed to set CTL");
        return false;
    }

    if (!aw9523->writeP0(p0_state)) {
        LOGGER.error("AW9523: Failed to set P0");
        return false;
    }

    if (!aw9523->writeP1(p1_state)) {
        LOGGER.error("AW9523: Failed to set P1");
        return false;
    }

    if (axp2101->isVBus()) {
        float voltage = 0.0f;
        axp2101->getVBusVoltage(voltage);
        LOGGER.info("AXP2101: VBus at {:.2f}", voltage);
    } else {
        LOGGER.warn("AXP2101: VBus disabled");
    }

    return true;
}

bool initPowerControl() {
    LOGGER.info("Init power control (AXP2101)");

    // Source: https://github.com/m5stack/M5Unified/blob/b8cfec7fed046242da7f7b8024a4e92004a51ff7/src/utility/Power_Class.cpp#L61
    aw9523->bitOnP1(0b10000000); // SY7088 boost enable

    /** AXP2101 usage
    Source: https://github.com/m5stack/M5Unified/blob/b8cfec7fed046242da7f7b8024a4e92004a51ff7/README.md?plain=1#L223
    |           |M5Stack<BR>CoreS3<BR>CoreS3SE| |
    |:---------:|:-----------------:|:---------:|
    | ALDO1     |VDD 1v8            | ALDO1     |
    | ALDO2     |VDDA 3v3           | ALDO2     |
    | ALDO3     |CAM 3v3            | ALDO3     |
    | ALDO4     |TF 3v3             | ALDO4     |
    | BLDO1     |AVDD               | BLDO1     |
    | BLDO2     |DVDD               | BLDO2     |
    | DLDO1/DC1 |LCD BL             | DLDO1/DC1 |
    | DLDO2/DC2 | ---               | DLDO2/DC2 |
    | BACKUP    |RTC BAT            | BACKUP    |
    */

    /**
     * 0x92 = ALD01
     * 0x93 = ALD02
     * 0x94 = ALD03
     * 0x95 = ALD04
     * 0x96 = BLD01
     * 0x97 = BLD02
     *
     * DCDC1 : 0.7-3.5V，  25mV/step  1200mA
     * DCDC2 : 0.7-2.275V，25mV/step  1600mA
     * DCDC3 : 0.7-3.5V，  25mV/step   700mA

     * LDOio0: 1.8-3.3V,  100mV/step    50mA
     * LDO1  :                          30mA always on
     * LDO2  : 1.8-3.3V， 100mV/step   200mA
     * LDO3  : 1.8-3.3V， 100mV/step   200mA
     */
    // Source: https://github.com/m5stack/M5Unified/blob/b8cfec7fed046242da7f7b8024a4e92004a51ff7/src/utility/Power_Class.cpp#L64
    static constexpr uint8_t reg_data_array[] = {
        0x90U, 0xBFU,  // LDOS ON/OFF control 0 (backlight)
        0x92U, 18U -5U, // ALDO1 set to 1.8v // for AW88298
        0x93U, 33U -5U, // ALDO2 set to 3.3v // for ES7210
        0x94U, 33U -5U, // ALDO3 set to 3.3v // for camera
        0x95U, 33U -5U, // ALDO3 set to 3.3v // for TF card slot
        0x27, 0x00, // PowerKey Hold=1sec / PowerOff=4sec
        0x69, 0x11, // CHGLED setting
        0x10, 0x30, // PMU common config
        0x30, 0x0F // ADC enabled (for voltage measurement)
    };

    if (axp2101->setRegisters((uint8_t*)reg_data_array, sizeof(reg_data_array))) {
        LOGGER.info("AXP2101 initialized with {} registers", sizeof(reg_data_array) / 2);
        return true;
    } else {
        LOGGER.error("AXP2101: Failed to set registers");
        return false;
    }
}

bool initBoot() {
    LOGGER.info("initBoot()");

    auto controller = device_find_by_name("i2c_internal");
    axp2101 = std::make_shared<Axp2101>(controller);
    aw9523 = std::make_shared<Aw9523>(controller);

    return initPowerControl() && initGpioExpander();
}