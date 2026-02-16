// SPDX-License-Identifier: Apache-2.0
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

struct Module platform_posix_module = {
    .name = "platform-posix",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
