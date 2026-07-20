#include <tactility/error.h>
#include <tactility/module.h>

#include <cstring>

extern "C" {

struct Module m5stack_cores3_module = {
    .name = "m5stack-cores3",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
