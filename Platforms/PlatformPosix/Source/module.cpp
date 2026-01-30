#include <tactility/module.h>

extern "C" {

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
