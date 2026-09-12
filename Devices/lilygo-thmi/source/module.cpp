#include <tactility/module.h>

extern "C" {

Module lilygo_thmi_module = {
    .name = "lilygo-thmi",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
