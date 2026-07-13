#pragma once

#include <Tactility/Mutex.h>
#include <Tactility/PubSub.h>
#include <Tactility/Timer.h>
#include <Tactility/service/Service.h>
#include <Tactility/service/ServiceContext.h>
#include <Tactility/service/audio/Audio.h>

#include <tactility/drivers/audio_stream.h>

#include <memory>

struct Device;

namespace tt::service::audio {

class AudioService final : public Service {

    Mutex mutex;
    Device* streamDevice = nullptr;
    std::shared_ptr<PubSub<AudioEvent>> pubsub = std::make_shared<PubSub<AudioEvent>>();
    std::unique_ptr<Timer> persistTimer;
    bool persistPending = false;

    void findStreamDevice();
    void primeFromSettings() const;
    void schedulePersist();
    void persistIfPending();
    void onDriverChanged(enum AudioCodecDirection direction, enum AudioStreamChange change);
    static void onDriverChangedCallback(Device* device, enum AudioCodecDirection direction, enum AudioStreamChange change, void* userData);

public:

    bool onStart(ServiceContext& serviceContext) override;
    void onStop(ServiceContext& serviceContext) override;

    bool isAvailable() const;

    /** @return true if a codec supporting input (microphone) is bound for this device */
    bool isInputAvailable() const;
    /** @return true if a codec supporting output (speaker) is bound for this device */
    bool isOutputAvailable() const;

    bool isInputEnabled() const;
    void setInputEnabled(bool enabled);

    bool isOutputEnabled() const;
    void setOutputEnabled(bool enabled);

    float getInputVolume() const;
    void setInputVolume(float percent);

    float getOutputVolume() const;
    void setOutputVolume(float percent);

    bool isInputMuted() const;
    void setInputMuted(bool muted);

    bool isOutputMuted() const;
    void setOutputMuted(bool muted);

    std::shared_ptr<PubSub<AudioEvent>> getPubsub() const { return pubsub; }
};

std::shared_ptr<AudioService> findAudioService();

} // namespace tt::service::audio
