#pragma once

#include <Tactility/hal/audio/AudioDevice.h>
#include <esp_codec_dev.h>
#include <driver/i2s_std.h>
#include <memory>

namespace waveshare::audio {

class AudioDevice : public tt::hal::audio::AudioDevice {

public:

    enum class Role {
        Speaker,
        Microphone
    };

    AudioDevice(Role role, esp_codec_dev_handle_t handle, i2s_chan_handle_t txChan, i2s_chan_handle_t rxChan);
    ~AudioDevice() override;

    std::string getName() const override;
    std::string getDescription() const override;

    Role getRole() const { return role; }
    esp_codec_dev_handle_t getHandle() const { return handle; }

    bool isValid() const { return handle != nullptr; }

    bool setVolume(uint8_t volume) override;
    uint8_t getVolume() const override { return currentVolume; }
    bool setMuted(bool muted) override;
    bool isMuted() const override { return currentMuted; }

    void* getCodecHandle() const override { return static_cast<void*>(handle); }
    void* getTxChanHandle() const override { return static_cast<void*>(txChan); }
    void* getRxChanHandle() const override { return static_cast<void*>(rxChan); }

    int read(void* buffer, size_t len, uint32_t timeoutMs) override;
    int write(const void* buffer, size_t len, uint32_t timeoutMs) override;
    bool enableChannels() override;
    bool disableChannels() override;

private:

    Role role;
    esp_codec_dev_handle_t handle;
    i2s_chan_handle_t txChan;
    i2s_chan_handle_t rxChan;
    uint8_t currentVolume = 70;
    bool currentMuted = false;
};

std::shared_ptr<AudioDevice> createSpeakerDevice();
std::shared_ptr<AudioDevice> createMicrophoneDevice();
bool initAudioCodecs(std::shared_ptr<AudioDevice> speaker, std::shared_ptr<AudioDevice> microphone);

}
