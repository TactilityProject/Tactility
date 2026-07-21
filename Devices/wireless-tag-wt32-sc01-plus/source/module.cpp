#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module wireless_tag_wt32_sc01_plus_module = {
    .name = "wireless-tag-wt32-sc01-plus",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
