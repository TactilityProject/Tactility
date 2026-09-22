// SPDX-License-Identifier: Apache-2.0

// C ABI over the audio service, exported to external ELF apps through the tactility-audio
// kernel module's symbol table. Keep signatures ABI-stable: apps compile against their own
// declarations.
//
// The module lives in this component (not Modules/) because the shims call the audio
// service, which is implemented here: the app layer links before every module archive, so
// no separate module may hold these symbol references.

#include <Tactility/service/audio/Audio.h>

#include <tactility/module.h>

extern "C" {

float tactility_audio_get_output_volume() {
    return tt::service::audio::getOutputVolume();
}

void tactility_audio_set_output_volume(float percent) {
    tt::service::audio::setOutputVolume(percent);
}

bool tactility_audio_is_output_muted() {
    return tt::service::audio::isOutputMuted();
}

void tactility_audio_set_output_muted(bool muted) {
    tt::service::audio::setOutputMuted(muted);
}

bool tactility_audio_consume_play_pause_request() {
    return tt::service::audio::consumePlayPauseRequest();
}

void tactility_audio_clear_play_pause_request() {
    tt::service::audio::clearPlayPauseRequest();
}

static const ModuleSymbol AUDIO_SERVICE_SYMBOLS[] = {
    DEFINE_MODULE_SYMBOL(tactility_audio_get_output_volume),
    DEFINE_MODULE_SYMBOL(tactility_audio_set_output_volume),
    DEFINE_MODULE_SYMBOL(tactility_audio_is_output_muted),
    DEFINE_MODULE_SYMBOL(tactility_audio_set_output_muted),
    DEFINE_MODULE_SYMBOL(tactility_audio_consume_play_pause_request),
    DEFINE_MODULE_SYMBOL(tactility_audio_clear_play_pause_request),
    MODULE_SYMBOL_TERMINATOR
};

Module tactility_audio_module = {
    .name = "tactility-audio",
    .start = nullptr,
    .stop = nullptr,
    .drivers = nullptr,
    .symbols = AUDIO_SERVICE_SYMBOLS,
    .internal = nullptr,
};

}
