#ifdef ESP_PLATFORM
#include <sdkconfig.h>
#endif

#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/module.h>

#include <soc/soc_caps.h>

extern "C" {

extern Driver esp32_adc_oneshot_driver;
extern Driver esp32_gpio_driver;
extern Driver esp32_i2c_driver;
extern Driver esp32_i2c_master_driver;
extern Driver esp32_i2s_driver;
#if SOC_LCD_I80_SUPPORTED
extern Driver esp32_i8080_driver;
#endif
extern Driver esp32_pwm_ledc_driver;
#if SOC_SDMMC_HOST_SUPPORTED
extern Driver esp32_sdmmc_driver;
#endif
extern Driver esp32_sdspi_driver;
extern Driver esp32_spi_driver;
extern Driver esp32_uart_driver;
extern Driver esp32_grove_driver;
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
extern Driver esp32_wifi_driver;
extern Driver esp32_wifi_pinned_driver;
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED)
extern Driver esp32_bluetooth_driver;
extern Driver esp32_ble_serial_driver;
extern Driver esp32_ble_midi_driver;
extern Driver esp32_ble_hid_device_driver;
#endif
#if SOC_USB_OTG_SUPPORTED
extern Driver esp32_usbhost_driver;
extern Driver esp32_usbhost_hid_driver;
extern Driver esp32_usbhost_midi_driver;
extern Driver esp32_usbhost_msc_driver;
#endif

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&esp32_adc_oneshot_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_gpio_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_i2c_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_i2c_master_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_i2s_driver) == ERROR_NONE);
#if SOC_LCD_I80_SUPPORTED
    check(driver_construct_add(&esp32_i8080_driver) == ERROR_NONE);
#endif
    check(driver_construct_add(&esp32_pwm_ledc_driver) == ERROR_NONE);
#if SOC_SDMMC_HOST_SUPPORTED
    check(driver_construct_add(&esp32_sdmmc_driver) == ERROR_NONE);
#endif
    check(driver_construct_add(&esp32_sdspi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_spi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_uart_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_grove_driver) == ERROR_NONE);
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    check(driver_construct_add(&esp32_wifi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_wifi_pinned_driver) == ERROR_NONE);
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    check(driver_construct_add(&esp32_bluetooth_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_ble_serial_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_ble_midi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_ble_hid_device_driver) == ERROR_NONE);
#endif
#if SOC_USB_OTG_SUPPORTED
    check(driver_construct_add(&esp32_usbhost_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_usbhost_hid_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_usbhost_midi_driver) == ERROR_NONE);
    check(driver_construct_add(&esp32_usbhost_msc_driver) == ERROR_NONE);
#endif
    return ERROR_NONE;
}

static error_t stop() {
    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
#if defined(CONFIG_SOC_WIFI_SUPPORTED) || defined(CONFIG_SLAVE_SOC_WIFI_SUPPORTED)
    check(driver_remove_destruct(&esp32_wifi_pinned_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_wifi_driver) == ERROR_NONE);
#endif
#if SOC_USB_OTG_SUPPORTED
    check(driver_remove_destruct(&esp32_usbhost_msc_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_usbhost_midi_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_usbhost_hid_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_usbhost_driver) == ERROR_NONE);
#endif
#if defined(CONFIG_BT_NIMBLE_ENABLED)
    check(driver_remove_destruct(&esp32_ble_hid_device_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_ble_midi_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_ble_serial_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_bluetooth_driver) == ERROR_NONE);
#endif
    check(driver_remove_destruct(&esp32_adc_oneshot_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_gpio_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_i2c_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_i2c_master_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_i2s_driver) == ERROR_NONE);
#if SOC_LCD_I80_SUPPORTED
    check(driver_remove_destruct(&esp32_i8080_driver) == ERROR_NONE);
#endif
    check(driver_remove_destruct(&esp32_pwm_ledc_driver) == ERROR_NONE);
#if SOC_SDMMC_HOST_SUPPORTED
    check(driver_remove_destruct(&esp32_sdmmc_driver) == ERROR_NONE);
#endif
    check(driver_remove_destruct(&esp32_sdspi_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_spi_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_uart_driver) == ERROR_NONE);
    check(driver_remove_destruct(&esp32_grove_driver) == ERROR_NONE);
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
