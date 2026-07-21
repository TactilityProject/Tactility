// SPDX-License-Identifier: Apache-2.0
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/drivers/esp32_sdcard.h>
#include <tactility/drivers/esp32_sdspi.h>

#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <tactility/drivers/esp32_sdmmc.h>
#endif

extern "C" {

sdmmc_card_t* esp32_sdcard_get_card(Device* device) {
    auto* driver = device_get_driver(device);
    if (driver_is_compatible(driver, "espressif,esp32-sdspi")) {
        return esp32_sdspi_get_card(device);
    }
#if SOC_SDMMC_HOST_SUPPORTED
    if (driver_is_compatible(driver, "espressif,esp32-sdmmc")) {
        return esp32_sdmmc_get_card(device);
    }
#endif
    return nullptr;
}

}
