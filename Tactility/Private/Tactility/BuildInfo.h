// SPDX-License-Identifier: Apache-2.0
#pragma once

extern "C" {
/** The git commit hash that the firmware was built from, empty when unknown */
extern const char tt_build_git_commit[];
/** When the firmware was built, in seconds since the Unix epoch (UTC) */
extern const long long tt_build_timestamp;
/** The release channel that the firmware was published to: "stable", "unstable" or "develop" */
extern const char tt_build_channel[];
}
