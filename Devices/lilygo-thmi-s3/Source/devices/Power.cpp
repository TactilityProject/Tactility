#include "Power.h"

#include <ChargeFromAdcVoltage.h>
#include <EstimatedPower.h>

std::shared_ptr<tt::hal::power::PowerDevice> createPower() {
    ChargeFromAdcVoltage::Configuration configuration;
    // 2.0 ratio, but +.11 added as display voltage sag compensation.
    configuration.adcMultiplier = 2.11;

    return std::make_shared<EstimatedPower>(configuration);
}