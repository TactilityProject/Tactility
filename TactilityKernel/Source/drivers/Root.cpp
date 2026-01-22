#include <Tactility/drivers/Root.h>
#include <Tactility/Driver.h>

extern "C" {

Driver root_driver = {
    .name = "root",
    .compatible = (const char*[]) { "root", nullptr },
    .start_device = nullptr,
    .stop_device = nullptr,
    .api = nullptr,
    .device_type = nullptr,
    .internal = { 0 }
};

}
