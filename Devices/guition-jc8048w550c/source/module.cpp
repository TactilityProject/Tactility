#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module guition_jc8048w550c_module = {
    .name = "guition-jc8048w550c",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
