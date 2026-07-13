#include <Tactility/hal/Configuration.h>

bool initBoot();

extern const tt::hal::Configuration hardwareConfiguration = {
    .initBoot = initBoot
};
