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

enum class InstallError {
    None,
    /** The image doesn't match what this device can install, so it is installed with the web installer */
    Incompatible,
    /** E.g. a failed download or flash write */
    Other,
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
    /** While installing: the download runs. Before it, the connection is made and the flash is erased. */
    bool downloading;
    /** The device can install an update itself. Without it, an update is installed with the web flasher. */
    bool installSupported;
    /** The release was built for another partition layout, or doesn't fit, so it is installed with the web flasher */
    bool fullInstallRequired;
    /** Why the last install failed, valid while the status is InstallFailed */
    InstallError installError;
    Channel channel;
};

/**
 * Queue a check for an update.
 * @param[in] force false to skip the check when the last one was less than 10 minutes ago
 */
void requestCheck(bool force);

/**
 * Queue the installation of the available update, also after an install failed
 * @param[in] gitCommit the commit of the release that the user confirmed
 * @retval ERROR_INVALID_STATE no update is available, or the available one isn't the confirmed release
 * @retval ERROR_NOT_SUPPORTED the device can't install updates itself, or the release needs a full install
 */
error_t requestInstall(const char* gitCommit);

/**
 * Queue a check that treats the published build of the channel as an update, also when it isn't newer, and installs it.
 * For testing updates.
 * @retval ERROR_NOT_SUPPORTED the device can't install updates itself
 * @retval ERROR_INVALID_STATE an update is installing or waits for a restart
 */
error_t requestForcedUpdate();

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
