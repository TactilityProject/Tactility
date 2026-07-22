#include <tactility/driver.h>
#include <tactility/module.h>

extern "C" {

extern Driver papers3_power_driver;
extern Driver papers3_power_supply_driver;
extern Driver papers3_display_driver;

static Driver* const papers3_drivers[] = {
    &papers3_display_driver,
    &papers3_power_driver,
    &papers3_power_supply_driver,
    nullptr
};

Module m5stack_papers3_module = {
    .name = "m5stack-papers3",
    .drivers = papers3_drivers
};

}
