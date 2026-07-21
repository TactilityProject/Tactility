#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module elecrow_crowpanel_basic_50_module = {
    .name = "elecrow-crowpanel-basic-50",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
