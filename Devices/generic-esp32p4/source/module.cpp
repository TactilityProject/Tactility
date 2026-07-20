#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module generic_esp32p4_module = {
    .name = "generic-esp32p4",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
