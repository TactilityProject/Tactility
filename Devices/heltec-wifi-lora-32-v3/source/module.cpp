#include <tactility/error.h>
#include <tactility/module.h>

extern "C" {

Module heltec_wifi_lora_32_v3_module = {
    .name = "heltec-wifi-lora-32-v3",
    .start = [] -> error_t { return ERROR_NONE; },
    .stop = [] -> error_t { return ERROR_NONE; },
    .symbols = nullptr,
    .internal = nullptr
};

}
