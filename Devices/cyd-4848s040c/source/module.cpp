#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module cyd_4848s040c_module = {
    .name = "cyd-4848s040c",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
