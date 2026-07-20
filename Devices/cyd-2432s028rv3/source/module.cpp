#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

struct Module cyd_2432s028rv3_module = {
    .name = "cyd-2432s028rv3",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
