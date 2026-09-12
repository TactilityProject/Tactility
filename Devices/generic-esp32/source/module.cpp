#include <tactility/module.h>

extern "C" {

Module generic_esp32_module = {
    .name = "generic-esp32",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = nullptr,
    .internal = nullptr
};

}
