// SPDX-License-Identifier: Apache-2.0
#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

struct Module lvgl_module = {
    .name = "lvgl",
    .start = start,
    .stop = stop
};

}
