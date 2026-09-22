#pragma once

#include <Tactility/PubSub.h>

#include <memory>

namespace tt::service::audio {

enum class AudioEvent {
    InputEnabledChanged,
    OutputEnabledChanged,
    InputVolumeChanged,
    OutputVolumeChanged,
    InputMuteChanged,
    OutputMuteChanged
};

/** @return the audio pubsub that broadcasts AudioEvent objects */
std::shared_ptr<PubSub<AudioEvent>> getPubsub();

/** @return true when an AUDIO_STREAM_TYPE device is bound and ready */
bool isAvailable();

/** @return true if a codec supporting input (microphone) is bound, e.g. a device with a speaker but no mic returns false */
bool isInputAvailable();

/** @return true if a codec supporting output (speaker) is bound, e.g. a device with a mic but no speaker returns false */
bool isOutputAvailable();

bool isInputEnabled();
void setInputEnabled(bool enabled);

bool isOutputEnabled();
void setOutputEnabled(bool enabled);

float getInputVolume();
void setInputVolume(float percent);

float getOutputVolume();
void setOutputVolume(float percent);

bool isInputMuted();
void setInputMuted(bool muted);

bool isOutputMuted();
void setOutputMuted(bool muted);

/**
 * Requests a media transport Play/Pause toggle, e.g. from a headset button or media
 * keyboard. Safe to call from any task. A media app consumes the request via
 * consumePlayPauseRequest() on its own thread (e.g. a poll timer) and toggles playback.
 */
void requestPlayPause();

/**
 * Returns and clears a pending Play/Pause request. Returns false when nothing was
 * requested; safe to poll.
 */
bool consumePlayPauseRequest();

/** Clears any pending Play/Pause request without acting on it (e.g. when a media app
 *  opens, so a request made while it was closed does not fire on open). */
void clearPlayPauseRequest();

} // namespace tt::service::audio
