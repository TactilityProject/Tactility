#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module generic_esp32s3_module = {
    .name = "generic-esp32s3",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
