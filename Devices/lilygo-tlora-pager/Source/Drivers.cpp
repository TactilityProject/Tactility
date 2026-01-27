#include <tactility/driver.h>

extern "C" {

extern void register_device_drivers() {
    extern Driver tlora_pager_driver;
    driver_construct(&tlora_pager_driver);
}

}
