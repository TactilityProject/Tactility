#include <tactility/module.h>

extern "C" {

Module btt_panda_touch_module = {
    .name = "btt-panda-touch",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
