#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module es3c28p_module = {
    .name = "es3c28p",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
