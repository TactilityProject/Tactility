#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module elecrow_crowpanel_advance_35_module = {
    .name = "elecrow-crowpanel-advance-35",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
