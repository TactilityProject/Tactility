#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module cyd_3248s035c_module = {
    .name = "cyd-3248s035c",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
