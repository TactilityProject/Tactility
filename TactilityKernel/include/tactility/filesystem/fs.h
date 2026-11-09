// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/filesystem/directory.h>
#include <tactility/filesystem/file.h>
#include <tactility/filesystem/path.h>

/**
 * @brief Path-level filesystem operations, all taking an already-resolved absolute path.
 * Includes path.h, directory.h and file.h.
 *
 * Distinct from file_system.h, which is the mount-level API (registering/mounting a
 * FileSystem). These wrap the platform's own POSIX-style calls directly. ESP-IDF's VFS makes
 * that work unmodified on-device, same as the simulator.
 */
