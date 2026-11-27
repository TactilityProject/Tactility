// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <tactility/concurrent/task_event_group.h>
#include <tactility/error.h>

#include <cstdint>

/**
 * Checks the CDN for a newer build of the operating system and installs it into the next OTA partition.
 * Checks and installs run on a background task. Subscribers are signalled when the state changes.
 */
namespace tt::service::firmwareupdate {

enum class Status {
    /** No check has finished yet */
    Unknown,
    Checking,
    UpToDate,
    /** The CDN has a newer build, see Snapshot::release */
    Available,
    CheckFailed,
    Installing,
    InstallFailed,
    /** The newer build is installed and runs after a restart */
    Installed,
};

enum class Channel {
    /** Releases */
    Stable,
    /** Builds of the main branch */
    Unstable,
    /** Builds of pull requests */
    Develop,
};

struct Release {
    char version[32];
    char gitCommit[48];
    /** When it was built, in seconds since the Unix epoch */
    int64_t created;
    uint32_t size;
};

struct Snapshot {
    Status status;
    /** The build on the CDN, valid from the first successful check */
    Release release;
    bool hasRelease;
    /** Percentage of the download while installing */
    uint8_t progress;
    /** The device can install an update itself. Without it, an update is installed with the web flasher. */
    bool installSupported;
    Channel channel;
};

/**
 * Queue a check for an update.
 * @param[in] force false to skip the check when the last one was less than 10 minutes ago
 */
void requestCheck(bool force);

/**
 * Queue the installation of the available update, also after an install failed
 * @retval ERROR_INVALID_STATE no update is available
 * @retval ERROR_NOT_SUPPORTED the device can't install updates itself
 */
error_t requestInstall();

Snapshot getSnapshot();

/** Writes storage, so it must not be called on the LVGL thread. A change discards the found update and checks again. */
void setChannel(Channel channel);

/**
 * Signal a bit of @a eventGroup whenever the state changes
 * @param[out] outBit the claimed bit
 */
error_t subscribe(TaskEventGroup* eventGroup, uint32_t* outBit);

void unsubscribe(TaskEventGroup* eventGroup, uint32_t bit);

} // namespace tt::service::firmwareupdate
