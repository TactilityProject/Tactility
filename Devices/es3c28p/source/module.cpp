#include <tactility/module.h>
#include <Tactility/hal/Configuration.h>

// Legacy placeholder (required until legacy HAL is cleaned up everywhere)
extern const tt::hal::Configuration hardwareConfiguration = {};

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module es3c28p_module = {
    .name = "es3c28p",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
