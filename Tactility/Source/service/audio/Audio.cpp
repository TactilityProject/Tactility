#include <Tactility/service/audio/Audio.h>
#include <Tactility/service/audio/AudioService.h>
#include <Tactility/service/ServiceManifest.h>
#include <Tactility/service/ServiceRegistration.h>

#include <atomic>

namespace tt::service::audio {

extern const ServiceManifest manifest;

// The service is only registered when audio hardware is present (see Tactility.cpp);
// treat "not registered" as "no device bound" rather than asserting via findAudioService().
static std::shared_ptr<AudioService> tryFindAudioService() {
    return findServiceById<AudioService>(manifest.id);
}

std::shared_ptr<PubSub<AudioEvent>> getPubsub() {
    if (auto service = tryFindAudioService()) {
        return service->getPubsub();
    }
    // No audio hardware, service was never registered: hand back an inert pubsub
    // that nothing ever publishes to, so callers can subscribe/unsubscribe safely.
    static auto fallbackPubsub = std::make_shared<PubSub<AudioEvent>>();
    return fallbackPubsub;
}

bool isAvailable() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isAvailable();
}

bool isInputAvailable() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isInputAvailable();
}

bool isOutputAvailable() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isOutputAvailable();
}

bool isInputEnabled() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isInputEnabled();
}

void setInputEnabled(bool enabled) {
    if (auto service = tryFindAudioService()) {
        service->setInputEnabled(enabled);
    }
}

bool isOutputEnabled() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isOutputEnabled();
}

void setOutputEnabled(bool enabled) {
    if (auto service = tryFindAudioService()) {
        service->setOutputEnabled(enabled);
    }
}

float getInputVolume() {
    auto service = tryFindAudioService();
    return service != nullptr ? service->getInputVolume() : 0.0f;
}

void setInputVolume(float percent) {
    if (auto service = tryFindAudioService()) {
        service->setInputVolume(percent);
    }
}

float getOutputVolume() {
    auto service = tryFindAudioService();
    return service != nullptr ? service->getOutputVolume() : 0.0f;
}

void setOutputVolume(float percent) {
    if (auto service = tryFindAudioService()) {
        service->setOutputVolume(percent);
    }
}

bool isInputMuted() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isInputMuted();
}

void setInputMuted(bool muted) {
    if (auto service = tryFindAudioService()) {
        service->setInputMuted(muted);
    }
}

bool isOutputMuted() {
    auto service = tryFindAudioService();
    return service != nullptr && service->isOutputMuted();
}

void setOutputMuted(bool muted) {
    if (auto service = tryFindAudioService()) {
        service->setOutputMuted(muted);
    }
}

// Media transport request latch: set from any task (e.g. the USB HID consumer routing a
// headset Play/Pause button), consumed on the media app's own thread. A latch rather than
// a pubsub topic keeps the surface minimal: one expected consumer (the foreground media
// app) polls it.
static std::atomic<bool> s_play_pause_requested(false);

void requestPlayPause() {
    // Toggle rather than store(true): each call represents one physical toggle-press, so two
    // presses before consumption must cancel out (net "no pending toggle"), not collapse to a
    // single one.
    bool expected = s_play_pause_requested.load();
    while (!s_play_pause_requested.compare_exchange_weak(expected, !expected)) {}
}

bool consumePlayPauseRequest() {
    return s_play_pause_requested.exchange(false);
}

void clearPlayPauseRequest() {
    s_play_pause_requested.store(false);
}

} // namespace tt::service::audio
