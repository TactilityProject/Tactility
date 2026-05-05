#include "AudioDevice.h"

#include <tactility/device.h>
#include <tactility/drivers/esp32_i2c.h>
#include <tactility/log.h>

#include <bsp/esp32_s3_touch_amoled_2_06.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev_defaults.h>
#include <es7210_adc.h>

namespace waveshare::audio {

#define TAG "WaveshareAudio"

static i2s_chan_handle_t s_i2s_tx_chan = nullptr;
static i2s_chan_handle_t s_i2s_rx_chan = nullptr;
static const audio_codec_data_if_t* s_i2s_data_if = nullptr;
static bool s_i2s_initialized = false;

static esp_codec_dev_sample_info_t DEFAULT_FS = {
    .bits_per_sample = 16,
    .channel = 2,
    .channel_mask = 0,
    .sample_rate = 22050,
    .mclk_multiple = 0,
};

#define I2S_DUPLEX_MONO_CFG(_sample_rate)                                                         \
    {                                                                                                 \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_sample_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), \
        .gpio_cfg = {                                                                                 \
            .mclk = BSP_I2S_MCLK,                                                                     \
            .bclk = BSP_I2S_SCLK,                                                                     \
            .ws = BSP_I2S_LCLK,                                                                       \
            .dout = BSP_I2S_DOUT,                                                                     \
            .din = BSP_I2S_DSIN,                                                                      \
            .invert_flags = {                                                                         \
                .mclk_inv = false,                                                                    \
                .bclk_inv = false,                                                                    \
                .ws_inv = false,                                                                      \
            },                                                                                        \
        },                                                                                            \
    }

static bool initI2S() {
    if (s_i2s_initialized) {
        return true;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_i2s_tx_chan, &s_i2s_rx_chan);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to create I2S channel: %d", err);
        return false;
    }

    const i2s_std_config_t std_cfg = I2S_DUPLEX_MONO_CFG(22050);
    if (s_i2s_tx_chan) {
        err = i2s_channel_init_std_mode(s_i2s_tx_chan, &std_cfg);
        if (err != ESP_OK) {
            LOG_E(TAG, "I2S TX channel init failed: %d", err);
            return false;
        }
        err = i2s_channel_enable(s_i2s_tx_chan);
        if (err != ESP_OK) {
            LOG_E(TAG, "I2S TX channel enable failed: %d", err);
            return false;
        }
    }
    if (s_i2s_rx_chan) {
        err = i2s_channel_init_std_mode(s_i2s_rx_chan, &std_cfg);
        if (err != ESP_OK) {
            LOG_E(TAG, "I2S RX channel init failed: %d", err);
            return false;
        }
        err = i2s_channel_enable(s_i2s_rx_chan);
        if (err != ESP_OK) {
            LOG_E(TAG, "I2S RX channel enable failed: %d", err);
            return false;
        }
    }

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_1,
        .rx_handle = s_i2s_rx_chan,
        .tx_handle = s_i2s_tx_chan,
    };
    s_i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!s_i2s_data_if) {
        LOG_E(TAG, "Failed to create I2S data interface");
        return false;
    }

    s_i2s_initialized = true;
    LOG_I(TAG, "I2S initialized (channel enable handled by esp_codec_dev open/close)");
    return true;
}

static i2c_master_bus_handle_t getKernelI2cBus() {
    ::Device* i2c = device_find_by_name("i2c0");
    if (!i2c) {
        LOG_E(TAG, "i2c0 not found");
        return nullptr;
    }
    if (!device_is_ready(i2c)) {
        LOG_E(TAG, "i2c0 not ready");
        return nullptr;
    }
    return esp32_i2c_get_bus_handle(i2c);
}

AudioDevice::AudioDevice(Role role, esp_codec_dev_handle_t handle, i2s_chan_handle_t txChan, i2s_chan_handle_t rxChan)
    : role(role)
    , handle(handle)
    , txChan(txChan)
    , rxChan(rxChan)
{}

AudioDevice::~AudioDevice() {
    if (handle) {
        esp_codec_dev_close(handle);
        esp_codec_dev_delete(handle);
        handle = nullptr;
    }
}

std::string AudioDevice::getName() const {
    return role == Role::Speaker ? "ES8311Speaker" : "ES7210Mic";
}

std::string AudioDevice::getDescription() const {
    return role == Role::Speaker
        ? "ES8311 audio codec for speaker output"
        : "ES7210 audio codec for microphone input";
}

bool AudioDevice::setVolume(uint8_t volume) {
    if (!handle) {
        return false;
    }
    if (volume > 100) {
        volume = 100;
    }
    currentVolume = volume;

    if (role == Role::Speaker) {
        esp_err_t err = esp_codec_dev_set_out_vol(handle, static_cast<float>(volume));
        if (err != ESP_OK) {
            LOG_E(TAG, "Failed to set volume: %d", err);
            return false;
        }
        err = esp_codec_dev_set_out_mute(handle, false);
        if (err != ESP_OK) {
            LOG_E(TAG, "Failed to unmute: %d", err);
            return false;
        }
    }

    return true;
}

bool AudioDevice::setMuted(bool muted) {
    currentMuted = muted;
    if (!handle) {
        return false;
    }

    if (role == Role::Speaker) {
        esp_err_t err = esp_codec_dev_set_out_mute(handle, muted);
        if (err != ESP_OK) {
            LOG_E(TAG, "Failed to set mute: %d", err);
            return false;
        }
    }

    return true;
}

int AudioDevice::read(void* buffer, size_t len, uint32_t timeoutMs) {
    if (!handle || role != Role::Microphone) return -1;
    int ret = esp_codec_dev_read(handle, buffer, len);
    return (ret == ESP_OK) ? static_cast<int>(len) : -1;
}

int AudioDevice::write(const void* buffer, size_t len, uint32_t timeoutMs) {
    if (!handle || role != Role::Speaker) return -1;
    int ret = esp_codec_dev_write(handle, const_cast<void*>(buffer), len);
    return (ret == ESP_OK) ? static_cast<int>(len) : -1;
}

bool AudioDevice::enableChannels() {
    if (!handle) return false;

    // Close and re-open with default format to re-enable
    esp_codec_dev_close(handle);
    esp_err_t err = esp_codec_dev_open(handle, &DEFAULT_FS);
    if (err != ESP_OK) {
        LOG_E(TAG, "Failed to re-open codec: %d", err);
        return false;
    }
    return true;
}

bool AudioDevice::disableChannels() {
    if (!handle) return false;
    esp_err_t err = esp_codec_dev_close(handle);
    return (err == ESP_OK);
}

std::shared_ptr<AudioDevice> createSpeakerDevice() {
    auto* bus_handle = getKernelI2cBus();
    if (!bus_handle) {
        LOG_E(TAG, "Cannot init speaker - no kernel I2C bus");
        return nullptr;
    }

    if (!initI2S()) {
        LOG_E(TAG, "Failed to init I2S");
        return nullptr;
    }

    const audio_codec_gpio_if_t* gpio_if = audio_codec_new_gpio();

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus_handle,
    };
    const audio_codec_ctrl_if_t* i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!i2c_ctrl_if) {
        LOG_E(TAG, "Failed to create I2C control interface for ES8311");
        return nullptr;
    }

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
        .pa_gain = 0,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
        .no_dac_ref = false,
        .mclk_div = 0,
    };
    const audio_codec_if_t* es8311_dev = es8311_codec_new(&es8311_cfg);
    if (!es8311_dev) {
        LOG_E(TAG, "Failed to create ES8311 codec interface");
        return nullptr;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = s_i2s_data_if,
    };
    esp_codec_dev_handle_t speaker = esp_codec_dev_new(&codec_dev_cfg);
    if (!speaker) {
        LOG_E(TAG, "Failed to create ES8311 codec device");
        return nullptr;
    }

    LOG_I(TAG, "ES8311 speaker codec created (not yet opened)");
    return std::make_shared<AudioDevice>(AudioDevice::Role::Speaker, speaker, s_i2s_tx_chan, s_i2s_rx_chan);
}

std::shared_ptr<AudioDevice> createMicrophoneDevice() {
    auto* bus_handle = getKernelI2cBus();
    if (!bus_handle) {
        LOG_E(TAG, "Cannot init microphone - no kernel I2C bus");
        return nullptr;
    }

    if (!initI2S()) {
        LOG_E(TAG, "Failed to init I2S");
        return nullptr;
    }

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = bus_handle,
    };
    const audio_codec_ctrl_if_t* i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!i2c_ctrl_if) {
        LOG_E(TAG, "Failed to create I2C control interface for ES7210");
        return nullptr;
    }

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .master_mode = false,
        .mic_selected = ES7120_SEL_MIC1,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 0,
    };
    const audio_codec_if_t* es7210_dev = es7210_codec_new(&es7210_cfg);
    if (!es7210_dev) {
        LOG_E(TAG, "Failed to create ES7210 codec interface");
        return nullptr;
    }

    esp_codec_dev_cfg_t codec_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es7210_dev,
        .data_if = s_i2s_data_if,
    };
    esp_codec_dev_handle_t mic = esp_codec_dev_new(&codec_dev_cfg);
    if (!mic) {
        LOG_E(TAG, "Failed to create ES7210 codec device");
        return nullptr;
    }

    LOG_I(TAG, "ES7210 microphone codec created (not yet opened)");
    return std::make_shared<AudioDevice>(AudioDevice::Role::Microphone, mic, s_i2s_tx_chan, s_i2s_rx_chan);
}

bool initAudioCodecs(std::shared_ptr<AudioDevice> speaker, std::shared_ptr<AudioDevice> microphone) {
    auto spkHandle = speaker ? static_cast<esp_codec_dev_handle_t>(speaker->getCodecHandle()) : nullptr;
    auto micHandle = microphone ? static_cast<esp_codec_dev_handle_t>(microphone->getCodecHandle()) : nullptr;

    // Close both first (same order as bsp_extra_codec_set_fs)
    if (spkHandle) esp_codec_dev_close(spkHandle);
    if (micHandle) esp_codec_dev_close(micHandle);

    // Set mic gain before opening
    if (micHandle) esp_codec_dev_set_in_gain(micHandle, 24.0f);

    // Open speaker first, then mic (both with same format)
    if (spkHandle) {
        esp_err_t err = esp_codec_dev_open(spkHandle, &DEFAULT_FS);
        if (err != ESP_OK) {
            LOG_E(TAG, "Failed to open speaker: %d", err);
            return false;
        }
    }
    if (micHandle) {
        esp_err_t err = esp_codec_dev_open(micHandle, &DEFAULT_FS);
        if (err != ESP_OK) {
            LOG_E(TAG, "Failed to open microphone: %d", err);
            return false;
        }
    }

    LOG_I(TAG, "Audio codecs opened together at %lu Hz", (unsigned long)DEFAULT_FS.sample_rate);
    return true;
}

}
