#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module cyd_e32r32p_module = {
    .name = "cyd-e32r32p",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
