#include <tactility/module.h>

extern "C" {

Module lilygo_tdisplay_module = {
    .name = "lilygo-tdisplay",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
