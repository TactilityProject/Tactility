// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <esp_blockdev.h>
#include <tactility/device.h>
#include <tactility/drivers/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * SPI NAND flash, formatted with FAT and exposed as a fixed (internal) SD-card-style volume.
 *
 * Board-specific wiring comes from devicetree. The FAT filesystem is provided by the
 * espressif/spi_nand_flash_fatfs integration, which puts Dhara wear leveling + bad block
 * management underneath.
 */
struct SpiNandConfig {
    /** SPI host (spi_host_device_t), e.g. SPI3_HOST */
    int32_t host;
    /** NAND clock */
    struct GpioPinSpec pin_clk;
    /** NAND DI (data in / D0) */
    struct GpioPinSpec pin_mosi;
    /** NAND DO (data out / D1) */
    struct GpioPinSpec pin_miso;
    /** NAND chip select */
    struct GpioPinSpec pin_cs;
    /** NAND WP# (optional) */
    struct GpioPinSpec pin_wp;
    /** NAND HOLD# (optional) */
    struct GpioPinSpec pin_hd;
    /** VFS mount path, e.g. "/sdcard" */
    const char* mount_path;
    /** File system name, e.g. "sdcard" */
    const char* mount_name;
    /** SPI clock in kHz */
    uint32_t max_freq_khz;
    /** Dhara spare-block ratio. Lower values reduce capacity and increase performance. */
    uint8_t gc_factor;
    /** Format the volume when it has no valid FAT filesystem (first boot) */
    bool format_if_mount_failed;
};

/**
 * @brief Gets the wear-levelled block device backing the mounted NAND volume.
 *
 * Used to hand the raw volume to USB mass storage. Returns
 * ESP_BLOCKDEV_HANDLE_INVALID when no NAND volume is active.
 */
esp_blockdev_handle_t spi_nand_get_user_storage_blockdev(void);

/**
 * @brief Unmounts the local FAT volume (if mounted) so a USB host can own the block device.
 *
 * @retval ERROR_NONE when unmounted or already unmounted
 * @retval (other) the filesystem unmount failed
 */
error_t spi_nand_suspend_user_storage(void);

/**
 * @brief Mounts the local FAT volume again after a USB host released it.
 *
 * @retval ERROR_NONE when mounted or already mounted
 * @retval (other) the filesystem mount failed
 */
error_t spi_nand_resume_user_storage(void);

/**
 * @brief Commits pending writes made through the USB mass-storage block device.
 *
 * The wrapper also syncs shortly after each write burst, so this is only needed to flush
 * immediately (e.g. when leaving USB drive mode).
 */
error_t spi_nand_sync_user_storage(void);

#ifdef __cplusplus
}
#endif
