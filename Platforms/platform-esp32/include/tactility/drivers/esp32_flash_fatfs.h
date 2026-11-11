// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Esp32FlashFatfsConfig {
    /** Label of the data partition in the partition table */
    const char* partition_label;
    /** VFS mount path */
    const char* mount_path;
};

#ifdef __cplusplus
}
#endif
