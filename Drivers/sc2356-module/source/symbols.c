// SPDX-License-Identifier: Apache-2.0
#include <drivers/sc2356.h>
#include <tactility/module.h>

const struct ModuleSymbol sc2356_module_symbols[] = {
    DEFINE_MODULE_SYMBOL(sc2356_open),
    DEFINE_MODULE_SYMBOL(sc2356_close),
    DEFINE_MODULE_SYMBOL(sc2356_get_frame),
    DEFINE_MODULE_SYMBOL(sc2356_release_frame),
    DEFINE_MODULE_SYMBOL(sc2356_get_width),
    DEFINE_MODULE_SYMBOL(sc2356_get_height),
    DEFINE_MODULE_SYMBOL(sc2356_set_rotation),
    DEFINE_MODULE_SYMBOL(sc2356_capture_jpeg),
    MODULE_SYMBOL_TERMINATOR
};
