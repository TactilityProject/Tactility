#pragma once

#include <tactility/hal/Device.h>
#include <cstdint>
#include <cstddef>

namespace tt::hal::audio {

class AudioDevice : public Device {

public:

    AudioDevice() = default;
    ~AudioDevice() override = default;

    Type getType() const override { return Type::Audio; }

    virtual bool setVolume(uint8_t volume) = 0;
    virtual uint8_t getVolume() const = 0;
    virtual bool setMuted(bool muted) = 0;
    virtual bool isMuted() const = 0;

    /** @return underlying codec handle for direct read/write, or nullptr if not supported */
    virtual void* getCodecHandle() const { return nullptr; }

    /** @return underlying I2S TX channel handle, or nullptr if not supported */
    virtual void* getTxChanHandle() const { return nullptr; }

    /** @return underlying I2S RX channel handle, or nullptr if not supported */
    virtual void* getRxChanHandle() const { return nullptr; }

    /** Read raw audio data (for microphone devices) */
    virtual int read(void* buffer, size_t len, uint32_t timeoutMs) { return -1; }

    /** Write raw audio data (for speaker devices) */
    virtual int write(const void* buffer, size_t len, uint32_t timeoutMs) { return -1; }

    /** Enable the I2S channels */
    virtual bool enableChannels() { return false; }

    /** Disable the I2S channels */
    virtual bool disableChannels() { return false; }
};

}
