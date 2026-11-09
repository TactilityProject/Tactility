// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "spi_nand_flash.h"

/* The NAND driver serializes transactions. Keep page data, configuration,
 * program and erase commands on the interrupt/DMA path; never retry an error
 * through the other API. Full-duplex transfers retain their original path. */
static inline esp_err_t spi_nand_transmit(const spi_nand_flash_config_t *config,
                                         spi_transaction_t *transaction)
{
    const bool short_read = transaction->length == 0 &&
        ((transaction->cmd == 0x13 && transaction->rxlength == 0) ||
         (transaction->cmd == 0x0f && transaction->addr == 0xc0 && transaction->rxlength == 8));
    if (config->poll_short_commands && (config->flags & SPI_DEVICE_HALFDUPLEX) && short_read) {
        return spi_device_polling_transmit(config->device_handle, transaction);
    }
    return spi_device_transmit(config->device_handle, transaction);
}
