// SPDX-License-Identifier: Apache-2.0
#include <tactility/module.h>

extern "C" {

static error_t start() {
    return ERROR_NONE;
}

static error_t stop() {
    return ERROR_NONE;
}

Module crypt_module = {
    .name = "crypt",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
