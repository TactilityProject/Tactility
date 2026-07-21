#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module cyd_2432s032c_module = {
    .name = "cyd-2432s032c",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
