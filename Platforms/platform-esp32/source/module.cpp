#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/module.h>

#include <soc/soc_caps.h>

extern "C" {

extern Driver esp32_gpio_driver;
extern Driver esp32_i2c_driver;
extern Driver esp32_i2s_driver;
#if SOC_SDMMC_HOST_SUPPORTED
extern Driver esp32_sdmmc_driver;
#endif
extern Driver esp32_spi_driver;
extern Driver esp32_uart_driver;
#if defined(CONFIG_BT_NIMBLE_ENABLED)
extern Driver esp32_bluetooth_driver;
extern Driver esp32_ble_serial_driver;
extern Driver esp32_ble_midi_driver;
extern Driver esp32_ble_hid_device_driver;
#endif

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&esp32_gpio_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_i2c_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_i2s_driver) == ERROR_NONE);
#if SOC_SDMMC_HOST_SUPPORTED
    check(driver_construct_add(&esp32_sdmmc_driver) == ERROR_NONE);
#endif
    check(driver_construct_add(&esp32_spi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_uart_driver) == ERROR_NONE);
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    check(driver_construct_add(&esp32_bluetooth_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_ble_serial_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_ble_midi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_ble_hid_device_driver) == ERROR_NONE);
#endif
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    check(driver_remove_destruct(&esp32_ble_hid_device_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_ble_midi_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_ble_serial_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_bluetooth_driver) == ERROR_NONE);
#endif
    check(driver_remove_destruct(&esp32_gpio_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_i2c_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_i2s_driver) == ERROR_NONE);
#if SOC_SDMMC_HOST_SUPPORTED
    check(driver_remove_destruct(&esp32_sdmmc_driver) == ERROR_NONE);
#endif
    check(driver_remove_destruct(&esp32_spi_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_uart_driver) == ERROR_NONE);
    return ERROR_NONE;
}

Module platform_esp32_module = {
    .name = "platform-esp32",
    .start = start,
    .stop = stop,
    .symbols = nullptr,
    .internal = nullptr
};

}
