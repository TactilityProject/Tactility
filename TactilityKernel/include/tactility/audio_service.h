// SPDX-License-Identifier: Apache-2.0

#pragma once

#ifndef __cplusplus
#include <stdbool.h>
#endif

// C ABI over the Tactility app-framework's audio service (output volume/mute, play-pause
// latch), exported to side-loaded ELF apps through the "tactility-audio" kernel module's symbol
// table (see Tactility/Source/service/audio/AudioExports.cpp in the firmware repo, which
// implements these against tt::service::audio::*). ABI-stable: an app compiles against these
// declarations and the ELF loader resolves the calls against the running firmware's exports.
//
// This is service-layer, not a kernel driver - it doesn't touch a Device directly, it reflects
// the app-facing audio service's state (the active output codec, whichever one that is).

#ifdef __cplusplus
extern "C" {
#endif

float tactility_audio_get_output_volume();
void tactility_audio_set_output_volume(float percent);
bool tactility_audio_is_output_muted();
void tactility_audio_set_output_muted(bool muted);
bool tactility_audio_consume_play_pause_request();
void tactility_audio_clear_play_pause_request();

#ifdef __cplusplus
}
#endif
