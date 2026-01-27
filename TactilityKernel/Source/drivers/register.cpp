// SPDX-License-Identifier: Apache-2.0

#include <tactility/driver.h>

extern "C" {

extern void register_kernel_drivers() {
    extern Driver root_driver;
    driver_construct(&root_driver);
}

}
