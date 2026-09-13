#ifndef _EPAPER_COMMON_H_
#define _EPAPER_COMMON_H_

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "epaper_spi.h"
#include "epaper_panel.h"

// Forward declaration
typedef struct epd_device epd_device_t;

/*=============================================================================
 * Device Access Functions (implemented in epaper.c)
 *============================================================================*/
epd_spi_t* epd_get_spi(epd_device_t *dev);
uint16_t epd_get_width(epd_device_t *dev);
uint16_t epd_get_height(epd_device_t *dev);
const epd_panel_desc_t* epd_get_panel(epd_device_t *dev);
bool epd_is_partial_ready(epd_device_t *dev);

/*=============================================================================
 * Common SPI Macros
 *============================================================================*/
#define CMD(spi, c)     epd_spi_write_cmd(spi, c)
#define DATA(spi, d)    epd_spi_write_data(spi, d)
#define RESET(spi)      epd_spi_reset(spi)

/*=============================================================================
 * Busy Wait with Polarity Support
 *============================================================================*/

// Wait for panel ready, respecting busy signal polarity
// caps: panel capability flags (use EPD_CAP_BUSY_INV for inverted)
// timeout_ms: timeout in ms, 0 = infinite
void epd_wait_busy_polarity(epd_spi_t *spi, uint32_t caps, uint32_t timeout_ms);

// Convenience macro for controllers
#define WAIT(dev) epd_wait_busy_polarity(epd_get_spi(dev), epd_get_panel(dev)->caps, 0)

#endif // _EPAPER_COMMON_H_
