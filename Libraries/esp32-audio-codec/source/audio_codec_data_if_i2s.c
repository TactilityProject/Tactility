// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2s_controller.h>
#include <tactility/freertos/freertos.h>

#include <stdlib.h>

// esp_codec_dev_read/write may issue several of these chunked I2S transfers per call and
// gives us no way to forward the caller's audio_stream timeout through. Keep this short so
// a stalled/underrunning I2S link returns promptly -- callers retry on failure, and a long
// per-chunk timeout here is what made AudioStreamTest's loopback task unkillable (it blocked
// far longer than its read/write timeout and the 2s drain wait in stopLoopback()).
#define I2S_TIMEOUT_TICKS pdMS_TO_TICKS(200)

struct I2sDataContext {
    audio_codec_data_if_t base;
    struct Device* i2s_controller;
    bool is_open;
    bool is_pdm; // true if created via audio_codec_adapter_new_i2s_pdm_data()
};

static bool data_is_open(const audio_codec_data_if_t* handle) {
    const struct I2sDataContext* context = (const struct I2sDataContext*) handle;
    return context->is_open;
}

static int data_open(const audio_codec_data_if_t* handle, void* data_cfg, int cfg_size) {
    (void) data_cfg;
    (void) cfg_size;
    struct I2sDataContext* context = (struct I2sDataContext*) handle;
    context->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int data_enable(const audio_codec_data_if_t* handle, esp_codec_dev_type_t dev_type, bool enable) {
    struct I2sDataContext* context = (struct I2sDataContext*) handle;

    // esp_codec_dev_close() calls this with enable=false but never calls set_fmt() again
    // before the next open -- without releasing the channel here, a TDM/PDM RX channel
    // (e.g. ES7210, a PDM mic) or a TX channel stays enabled and holding its GPIOs/DMA
    // channel indefinitely after the app that opened it closes, starving other I2S/DMA
    // users system-wide. Only release the direction this codec actually owns -- a single
    // I2S controller can carry an independent TX user (e.g. a speaker amp) and RX user
    // (e.g. a PDM mic) at once, and tearing down the whole controller on one closing would
    // also kill the other's still-active stream.
    if (!enable) {
        bool isInput = (dev_type & ESP_CODEC_DEV_TYPE_IN) != 0;
        bool isOutput = (dev_type & ESP_CODEC_DEV_TYPE_OUT) != 0;
        if (isInput) {
            i2s_controller_disable_direction(context->i2s_controller, true);
        }
        if (isOutput) {
            i2s_controller_disable_direction(context->i2s_controller, false);
        }
    }

    return ESP_CODEC_DEV_OK;
}

static int data_set_fmt(const audio_codec_data_if_t* handle, esp_codec_dev_type_t dev_type, esp_codec_dev_sample_info_t* fs) {
    struct I2sDataContext* context = (struct I2sDataContext*) handle;
    if (!context->is_open || fs == NULL) {
        return ESP_CODEC_DEV_INVALID_ARG;
    }

    // PDM RX is a distinct setup path chosen at adapter-construction time (see
    // audio_codec_adapter_new_i2s_pdm_data()), not inferred from fs here -- PDM mics are
    // mono/stereo just like standard mode, so there's no sample-info heuristic that could
    // tell them apart the way fs->channel > 2 does for TDM.
    if (context->is_pdm) {
        // ESP-IDF's PDM RX mode only supports 16-bit samples -- silently configuring the
        // controller anyway would leave the caller's chosen bit depth misapplied.
        if (fs->bits_per_sample != 16) {
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        // PDM RX is fixed at 2 slots (mono or stereo) -- anything else doesn't map onto
        // I2S_SLOT_MODE_MONO/STEREO and would silently get treated as stereo otherwise.
        if (fs->channel != 1 && fs->channel != 2) {
            return ESP_CODEC_DEV_NOT_SUPPORT;
        }
        struct I2sPdmRxConfig pdm_config = {
            .sample_rate_hz = fs->sample_rate,
            .stereo = fs->channel == 2,
        };
        error_t error = i2s_controller_set_rx_pdm_config(context->i2s_controller, &pdm_config);
        if (error != ERROR_NONE) {
            return ESP_CODEC_DEV_DRV_ERR;
        }
        return ESP_CODEC_DEV_OK;
    }

    // Standard (stereo) mode and TDM mode are alternative setup paths for the same I2S
    // port, not sequential steps -- set_config() always allocates+enables a TX/RX channel
    // pair, and calling it before set_rx_tdm_config() leaves that pair's TX channel bound
    // to the shared BCLK/WS/MCLK pins, so the TDM path's fresh i2s_new_channel() can't
    // claim those pins ("occupied by i2s_driver" / "GPIO not usable" at runtime).
    if ((dev_type & ESP_CODEC_DEV_TYPE_IN) != 0 && fs->channel > 2) {
        struct I2sTdmRxConfig tdm_config = {
            .sample_rate_hz = fs->sample_rate,
            .mclk_multiple = (fs->mclk_multiple != 0) ? (uint32_t) fs->mclk_multiple : 256,
            .bclk_div = 8,
            .slot_count = fs->channel,
            .bits_per_sample = fs->bits_per_sample,
            .slot_bit_width = 0,
        };
        error_t error = i2s_controller_set_rx_tdm_config(context->i2s_controller, &tdm_config);
        if (error != ERROR_NONE) {
            return ESP_CODEC_DEV_DRV_ERR;
        }
        return ESP_CODEC_DEV_OK;
    }

    struct I2sConfig config = {
        .communication_format = I2S_FORMAT_STAND_I2S,
        .sample_rate = fs->sample_rate,
        .bits_per_sample = fs->bits_per_sample,
        .channel_left = 0,
        .channel_right = (fs->channel > 1) ? 1 : I2S_CHANNEL_NONE,
    };

    if (i2s_controller_set_config(context->i2s_controller, &config) != ERROR_NONE) {
        return ESP_CODEC_DEV_DRV_ERR;
    }

    return ESP_CODEC_DEV_OK;
}

static int data_read(const audio_codec_data_if_t* handle, uint8_t* data, int size) {
    struct I2sDataContext* context = (struct I2sDataContext*) handle;
    if (!context->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    size_t bytes_read = 0;
    error_t error = i2s_controller_read(context->i2s_controller, data, (size_t) size, &bytes_read, I2S_TIMEOUT_TICKS);
    if (error != ERROR_NONE) {
        return ESP_CODEC_DEV_READ_FAIL;
    }
    return (int) bytes_read;
}

static int data_write(const audio_codec_data_if_t* handle, uint8_t* data, int size) {
    struct I2sDataContext* context = (struct I2sDataContext*) handle;
    if (!context->is_open) {
        return ESP_CODEC_DEV_WRONG_STATE;
    }
    size_t bytes_written = 0;
    error_t error = i2s_controller_write(context->i2s_controller, data, (size_t) size, &bytes_written, I2S_TIMEOUT_TICKS);
    if (error != ERROR_NONE) {
        return ESP_CODEC_DEV_WRITE_FAIL;
    }
    return (int) bytes_written;
}

static int data_close(const audio_codec_data_if_t* handle) {
    struct I2sDataContext* context = (struct I2sDataContext*) handle;
    context->is_open = false;
    return ESP_CODEC_DEV_OK;
}

static const audio_codec_data_if_t* new_i2s_data(struct Device* i2s_controller, bool is_pdm) {
    if (i2s_controller == NULL) {
        return NULL;
    }

    struct I2sDataContext* context = (struct I2sDataContext*) calloc(1, sizeof(struct I2sDataContext));
    if (context == NULL) {
        return NULL;
    }

    context->i2s_controller = i2s_controller;
    context->is_open = false;
    context->is_pdm = is_pdm;
    context->base.open = data_open;
    context->base.is_open = data_is_open;
    context->base.enable = data_enable;
    context->base.set_fmt = data_set_fmt;
    context->base.read = data_read;
    context->base.write = data_write;
    context->base.close = data_close;

    return &context->base;
}

const audio_codec_data_if_t* audio_codec_adapter_new_i2s_data(struct Device* i2s_controller) {
    return new_i2s_data(i2s_controller, false);
}

const audio_codec_data_if_t* audio_codec_adapter_new_i2s_pdm_data(struct Device* i2s_controller) {
    return new_i2s_data(i2s_controller, true);
}

// Note: esp_codec_dev already provides audio_codec_delete_data_if() (calls ->close then frees);
// no adapter-specific cleanup is needed, so we don't redefine it here.
