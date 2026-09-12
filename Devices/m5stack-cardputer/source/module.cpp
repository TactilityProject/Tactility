#include <tactility/module.h>

extern "C" {

Module m5stack_cardputer_module = {
    .name = "m5stack-cardputer",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
