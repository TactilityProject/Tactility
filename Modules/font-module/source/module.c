// SPDX-License-Identifier: Apache-2.0
#include <font/module.h>

struct Module font_module = {
    .name = "font",
    .start = NULL,
    .stop = NULL,
    .drivers = NULL,
    .symbols = NULL,
    .internal = NULL,
};
