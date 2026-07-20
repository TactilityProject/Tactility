#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module m5stack_stackchan_module = {
    .name = "m5stack-stackchan",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
