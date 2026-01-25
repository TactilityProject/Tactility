#include "TloraPager.h"

#include <Tactility/Driver.h>

#include <esp_log.h>

extern "C" {

static int start(Device* device) {
    return 0;
}

static int stop(Device* device) {
    return 0;
}

Driver tlora_pager_driver = {
    .name = "T-Lora Pager",
    .compatible = (const char*[]) { "lilygo,tlora-pager", nullptr },
    .startDevice = start,
    .stopDevice = stop,
    .api = nullptr,
    .deviceType = nullptr,
    .internal = { 0 }
};

}
