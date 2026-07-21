#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module guition_jc1060p470ciwy_module = {
    .name = "guition-jc1060p470ciwy",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
