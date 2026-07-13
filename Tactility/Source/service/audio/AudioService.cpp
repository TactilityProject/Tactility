#include <Tactility/service/audio/AudioService.h>

#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>
#include <Tactility/settings/AudioSettings.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_codec.h>
#include <tactility/drivers/audio_stream.h>
#include <tactility/log.h>

namespace tt::service::audio {

constexpr auto* TAG = "AudioService";
extern const ServiceManifest manifest;

constexpr TickType_t PERSIST_INTERVAL_TICKS = pdMS_TO_TICKS(2000);

void AudioService::findStreamDevice() {
    streamDevice = nullptr;
    device_for_each_of_type(&AUDIO_STREAM_TYPE, &streamDevice, [](Device* device, void* context) -> bool {
        if (device_is_ready(device)) {
            *static_cast<Device**>(context) = device;
            return false;
        }
        return true;
    });
}

void AudioService::primeFromSettings() const {
    if (streamDevice == nullptr) {
        return;
    }

    auto settings = settings::audio::loadOrGetDefault();

    audio_stream_set_enabled(streamDevice, AUDIO_CODEC_DIR_INPUT, settings.inputEnabled);
    audio_stream_set_enabled(streamDevice, AUDIO_CODEC_DIR_OUTPUT, settings.outputEnabled);
    audio_stream_set_mute(streamDevice, AUDIO_CODEC_DIR_INPUT, settings.inputMuted);
    audio_stream_set_mute(streamDevice, AUDIO_CODEC_DIR_OUTPUT, settings.outputMuted);
    audio_stream_set_volume(streamDevice, AUDIO_CODEC_DIR_INPUT, settings.inputVolume);
    audio_stream_set_volume(streamDevice, AUDIO_CODEC_DIR_OUTPUT, settings.outputVolume);
}

void AudioService::schedulePersist() {
    auto lock = mutex.asScopedLock();
    lock.lock();
    persistPending = true;
}

void AudioService::persistIfPending() {
    bool shouldPersist;
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        shouldPersist = persistPending;
        persistPending = false;
        device = streamDevice;
    }

    if (!shouldPersist || device == nullptr) {
        return;
    }

    // audio_stream_get_*() leave their out-param untouched (not zeroed) when the
    // direction has no codec bound, returning ERROR_NOT_SUPPORTED instead -- starting
    // from a freshly default-constructed (uninitialized) AudioSettings and ignoring
    // those return values would persist garbage stack memory for the unsupported side.
    // Start from the existing saved settings so an unsupported direction's fields are
    // left at their last known-good value instead.
    settings::audio::AudioSettings settings = settings::audio::loadOrGetDefault();
    if (audio_stream_get_enabled(device, AUDIO_CODEC_DIR_INPUT, &settings.inputEnabled) == ERROR_NONE
        && audio_stream_get_mute(device, AUDIO_CODEC_DIR_INPUT, &settings.inputMuted) == ERROR_NONE) {
        audio_stream_get_volume(device, AUDIO_CODEC_DIR_INPUT, &settings.inputVolume);
    }
    if (audio_stream_get_enabled(device, AUDIO_CODEC_DIR_OUTPUT, &settings.outputEnabled) == ERROR_NONE
        && audio_stream_get_mute(device, AUDIO_CODEC_DIR_OUTPUT, &settings.outputMuted) == ERROR_NONE) {
        audio_stream_get_volume(device, AUDIO_CODEC_DIR_OUTPUT, &settings.outputVolume);
    }
    settings::audio::save(settings);
}

void AudioService::onDriverChangedCallback(Device* device, enum AudioCodecDirection direction, enum AudioStreamChange change, void* userData) {
    static_cast<AudioService*>(userData)->onDriverChanged(direction, change);
}

void AudioService::onDriverChanged(enum AudioCodecDirection direction, enum AudioStreamChange change) {
    bool isInput = (direction == AUDIO_CODEC_DIR_INPUT);
    AudioEvent event;
    switch (change) {
        case AUDIO_STREAM_CHANGE_VOLUME:
            event = isInput ? AudioEvent::InputVolumeChanged : AudioEvent::OutputVolumeChanged;
            break;
        case AUDIO_STREAM_CHANGE_MUTE:
            event = isInput ? AudioEvent::InputMuteChanged : AudioEvent::OutputMuteChanged;
            break;
        case AUDIO_STREAM_CHANGE_ENABLED:
        default:
            event = isInput ? AudioEvent::InputEnabledChanged : AudioEvent::OutputEnabledChanged;
            break;
    }

    // Called synchronously from the set*() caller's call stack, but AFTER it has already
    // released mutex (see setInputEnabled() etc.) -- pubsub->publish() below re-enters this
    // service (e.g. a subscriber's refresh() calling getOutputVolume()) on the same task,
    // which would deadlock on the non-recursive Mutex type if we were still holding it here.
    // Set the flag directly rather than via schedulePersist() to avoid taking mutex at all
    // in this already-sensitive path.
    persistPending = true;
    pubsub->publish(event);
}

bool AudioService::onStart(ServiceContext& serviceContext) {
    findStreamDevice();

    if (streamDevice == nullptr) {
        LOG_W(TAG, "No audio stream device found; service is unavailable");
        return true;
    }

    primeFromSettings();
    audio_stream_set_change_callback(streamDevice, &onDriverChangedCallback, this);

    persistTimer = std::make_unique<Timer>(Timer::Type::Periodic, PERSIST_INTERVAL_TICKS, [this] {
        persistIfPending();
    });
    persistTimer->start();

    return true;
}

void AudioService::onStop(ServiceContext& serviceContext) {
    if (persistTimer) {
        persistTimer->stop();
        persistTimer.reset();
    }
    persistIfPending();
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        if (streamDevice != nullptr) {
            audio_stream_set_change_callback(streamDevice, nullptr, nullptr);
        }
        streamDevice = nullptr;
    }
}

bool AudioService::isAvailable() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    return streamDevice != nullptr;
}

bool AudioService::isInputAvailable() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    bool supported = false;
    if (streamDevice != nullptr) {
        audio_stream_is_supported(streamDevice, AUDIO_CODEC_DIR_INPUT, &supported);
    }
    return supported;
}

bool AudioService::isOutputAvailable() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    bool supported = false;
    if (streamDevice != nullptr) {
        audio_stream_is_supported(streamDevice, AUDIO_CODEC_DIR_OUTPUT, &supported);
    }
    return supported;
}

bool AudioService::isInputEnabled() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    bool enabled = false;
    if (streamDevice != nullptr) {
        audio_stream_get_enabled(streamDevice, AUDIO_CODEC_DIR_INPUT, &enabled);
    }
    return enabled;
}

void AudioService::setInputEnabled(bool enabled) {
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        device = streamDevice;
    }
    if (device == nullptr) {
        return;
    }
    // Must not hold mutex here: this can synchronously trigger onDriverChanged() ->
    // pubsub->publish(), which re-enters this service (e.g. a subscriber's refresh()
    // calling isInputEnabled()) on the SAME task/call-stack. mutex is a non-recursive
    // FreeRTOS mutex, so holding it across this call deadlocks that re-entrant call.
    audio_stream_set_enabled(device, AUDIO_CODEC_DIR_INPUT, enabled);
}

bool AudioService::isOutputEnabled() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    bool enabled = false;
    if (streamDevice != nullptr) {
        audio_stream_get_enabled(streamDevice, AUDIO_CODEC_DIR_OUTPUT, &enabled);
    }
    return enabled;
}

void AudioService::setOutputEnabled(bool enabled) {
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        device = streamDevice;
    }
    if (device == nullptr) {
        return;
    }
    // See setInputEnabled() for why mutex must be released before this call.
    audio_stream_set_enabled(device, AUDIO_CODEC_DIR_OUTPUT, enabled);
}

float AudioService::getInputVolume() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    float volume = 0.0f;
    if (streamDevice != nullptr) {
        audio_stream_get_volume(streamDevice, AUDIO_CODEC_DIR_INPUT, &volume);
    }
    return volume;
}

void AudioService::setInputVolume(float percent) {
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        device = streamDevice;
    }
    if (device == nullptr) {
        return;
    }
    // See setInputEnabled() for why mutex must be released before this call.
    audio_stream_set_volume(device, AUDIO_CODEC_DIR_INPUT, percent);
}

float AudioService::getOutputVolume() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    float volume = 0.0f;
    if (streamDevice != nullptr) {
        audio_stream_get_volume(streamDevice, AUDIO_CODEC_DIR_OUTPUT, &volume);
    }
    return volume;
}

void AudioService::setOutputVolume(float percent) {
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        device = streamDevice;
    }
    if (device == nullptr) {
        return;
    }
    // See setInputEnabled() for why mutex must be released before this call.
    audio_stream_set_volume(device, AUDIO_CODEC_DIR_OUTPUT, percent);
}

bool AudioService::isInputMuted() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    bool muted = false;
    if (streamDevice != nullptr) {
        audio_stream_get_mute(streamDevice, AUDIO_CODEC_DIR_INPUT, &muted);
    }
    return muted;
}

void AudioService::setInputMuted(bool muted) {
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        device = streamDevice;
    }
    if (device == nullptr) {
        return;
    }
    // See setInputEnabled() for why mutex must be released before this call.
    audio_stream_set_mute(device, AUDIO_CODEC_DIR_INPUT, muted);
}

bool AudioService::isOutputMuted() const {
    auto lock = mutex.asScopedLock();
    lock.lock();
    bool muted = false;
    if (streamDevice != nullptr) {
        audio_stream_get_mute(streamDevice, AUDIO_CODEC_DIR_OUTPUT, &muted);
    }
    return muted;
}

void AudioService::setOutputMuted(bool muted) {
    Device* device;
    {
        auto lock = mutex.asScopedLock();
        lock.lock();
        device = streamDevice;
    }
    if (device == nullptr) {
        return;
    }
    // See setInputEnabled() for why mutex must be released before this call.
    audio_stream_set_mute(device, AUDIO_CODEC_DIR_OUTPUT, muted);
}

// Precondition: AudioService must already be registered and started. Audio.cpp's wrapper
// functions do NOT use this (they must tolerate the service never having been registered
// on devices without audio hardware) - this is for callers that require the service to exist.
std::shared_ptr<AudioService> findAudioService() {
    auto service = findServiceById(manifest.id);
    assert(service != nullptr);
    return std::static_pointer_cast<AudioService>(service);
}

extern const ServiceManifest manifest = {
    .id = "Audio",
    .createService = create<AudioService>
};

} // namespace tt::service::audio
