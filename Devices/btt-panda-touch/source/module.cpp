#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

struct Module btt_panda_touch_module = {
    .name = "btt-panda-touch",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
