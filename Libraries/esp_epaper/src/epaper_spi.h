#ifndef _EPAPER_SPI_H_
#define _EPAPER_SPI_H_

#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"
#include "epaper_config.h"

typedef struct {
    spi_device_handle_t spi;
    int pin_dc;
    int pin_rst;
    int pin_busy;
    int pin_cs;
} epd_spi_t;

esp_err_t epd_spi_init(epd_spi_t *spi, const epd_pin_config_t *pins, const epd_spi_config_t *spi_cfg);
esp_err_t epd_spi_deinit(epd_spi_t *spi);

void epd_spi_write_cmd(epd_spi_t *spi, uint8_t cmd);
void epd_spi_write_data(epd_spi_t *spi, uint8_t data);
void epd_spi_write_data_bulk(epd_spi_t *spi, const uint8_t *data, uint32_t len);

void epd_spi_reset(epd_spi_t *spi);
int epd_spi_is_busy(epd_spi_t *spi);
void epd_spi_wait_busy(epd_spi_t *spi, uint32_t timeout_ms);

#endif // _EPAPER_SPI_H_
