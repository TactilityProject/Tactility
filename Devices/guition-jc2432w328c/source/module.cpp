#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module guition_jc2432w328c_module = {
    .name = "guition-jc2432w328c",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
