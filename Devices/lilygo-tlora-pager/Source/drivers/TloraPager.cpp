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
    .start_device = start,
    .stop_device = stop,
    .api = nullptr,
    .device_type = nullptr,
    .owner = &device_module,
    .driver_private = nullptr
};

}
