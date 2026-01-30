#include <tactility/module.h>

extern "C" {

extern Driver esp32_gpio_driver;
extern Driver esp32_i2c_driver;

static error_t start() {
    /* NO-OP for now */
    return ERROR_NONE;
}

static error_t stop() {
    /* NO-OP for now */
    return ERROR_NONE;
}

// The name must be exactly "platform_module"
struct Module platform_module = {
    .name = "POSIX Platform",
    .start = start,
    .stop = stop
};

}
