# Local changes

This is a fork of `espressif/spi_nand_flash` **1.4.4**, copied from the official ESP-Mosaico
BSP (`I:\esp-mosaico-bsp-master\components\espressif__spi_nand_flash`) and selected with
`override_path` from `Tactility/idf_component.yml`.

## Why

The ESP32-S31 is a preview target. On it, the interrupt/DMA SPI path stalls on the two
zero-length transactions the driver uses for NAND status:

- `PAGE READ` (`0x13`, no data phase)
- `GET FEATURE` (`0x0F`, address `0xC0`, 1 status byte)

`wait_for_ready()` polls the status register after every page program/erase. Without the
fix the poll never completes, which shows up as a hang right after the first `f_mount`
(during FAT format), followed by a watchdog reset.

## Added API

`spi_nand_flash_config_t.poll_short_commands` — when true (and the device is half-duplex),
those two commands are sent with `spi_device_polling_transmit()` instead of
`spi_device_transmit()`. `Tactility` sets it in `Drivers/spi-nand-module`.

The fork also carries the BSP's optional Mosaico extensions
(`mosaico_nand_extension.h`: `mosaico_nand_prepare_quad()` for QIO bootstrap and
`mosaico_nand_clear_map()`); Tactility does not use them yet.

## Upstreaming

If a future `espressif/spi_nand_flash` release includes `poll_short_commands`, delete this
directory and point the dependency back at the registry version. Re-check the S31 behaviour
before removing the fork.
