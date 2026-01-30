#include "TloraPager.h"

#include <tactility/driver.h>

#include <esp_log.h>

extern "C" {

extern struct Module device_module;

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
    .owner = &device_module,
    .driver_private = nullptr
};

}
