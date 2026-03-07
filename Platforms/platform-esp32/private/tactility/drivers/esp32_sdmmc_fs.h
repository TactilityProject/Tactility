// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <soc/soc_caps.h>
#if SOC_SDMMC_HOST_SUPPORTED
#include <tactility/filesystem/file_system.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32SdmmcConfig;
typedef void* Esp32SdmmcHandle;

extern Esp32SdmmcHandle esp32_sdmmc_fs_alloc(const struct Esp32SdmmcConfig* config, const char* mount_path);

extern void esp32_sdmmc_fs_free(Esp32SdmmcHandle handle);

extern sdmmc_card_t* esp32_sdmmc_fs_get_card(Esp32SdmmcHandle handle);

extern const FileSystemApi esp32_sdmmc_fs_api;

#ifdef __cplusplus
}
#endif

#endif