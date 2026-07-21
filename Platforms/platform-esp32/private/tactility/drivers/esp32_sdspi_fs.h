// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <sd_protocol_types.h>
#include <tactility/filesystem/file_system.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SdspiConfig;
typedef void* Esp32SdspiHandle;

Esp32SdspiHandle esp32_sdspi_fs_alloc(const struct Esp32SdspiConfig* config, int spi_host, int cs_pin, const char* mount_path);
void esp32_sdspi_fs_free(Esp32SdspiHandle handle);
sdmmc_card_t* esp32_sdspi_fs_get_card(Esp32SdspiHandle handle);

extern const FileSystemApi esp32_sdspi_fs_api;

#ifdef __cplusplus
}
#endif
