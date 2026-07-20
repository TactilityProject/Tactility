#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module cyd_8048s043c_module = {
    .name = "cyd-8048s043c",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
