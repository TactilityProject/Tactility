#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <tactility/device.h>
#include <tactility/drivers/root.h>

// Inherit base config
#define tlora_pager_config root_config

// Inherit base API
#define tlora_pager_api root_api

int tlora_pager_init(const struct device* device);

int tlora_pager_deinit(const struct device* device);

#ifdef __cplusplus
}
#endif
