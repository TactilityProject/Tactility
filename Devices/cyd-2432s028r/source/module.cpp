#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

struct Module cyd_2432s028r_module = {
    .name = "cyd-2432s028r",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
