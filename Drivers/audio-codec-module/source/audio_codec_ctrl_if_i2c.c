// SPDX-License-Identifier: Apache-2.0
#include <tactility/drivers/audio_codec_adapters.h>
#include <tactility/drivers/i2c_controller.h>
#include <tactility/freertos/freertos.h>

#include <stdint.h>
#include <stdlib.h>

#define I2C_TIMEOUT_TICKS pdMS_TO_TICKS(100)

struct I2cCtrlContext {
    audio_codec_ctrl_if_t base;
    struct Device* i2c_controller;
    uint8_t address;
    bool is_open;
};

static bool ctrl_is_open(const audio_codec_ctrl_if_t* handle) {
    const struct I2cCtrlContext* context = (const struct I2cCtrlContext*) handle;
    return context->is_open;
}

static int ctrl_open(const audio_codec_ctrl_if_t* handle, void* config, int config_size) {
    (void) config;
    (void) config_size;
    struct I2cCtrlContext* context = (struct I2cCtrlContext*) handle;
    context->is_open = true;
    return ESP_CODEC_DEV_OK;
}

static int ctrl_read_reg(const audio_codec_ctrl_if_t* handle, int reg, int reg_len, void* data, int data_len) {
    struct I2cCtrlContext* context = (struct I2cCtrlContext*) handle;
    if (!context->is_open || reg_len != 1) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (data_len < 0 || data_len > UINT16_MAX) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    error_t error = i2c_controller_read_register(context->i2c_controller, context->address, (uint8_t) reg, (uint8_t*) data, (size_t) data_len, I2C_TIMEOUT_TICKS);
    return (error == ERROR_NONE) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_READ_FAIL;
}

static int ctrl_write_reg(const audio_codec_ctrl_if_t* handle, int reg, int reg_len, void* data, int data_len) {
    struct I2cCtrlContext* context = (struct I2cCtrlContext*) handle;
    if (!context->is_open || reg_len != 1) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    if (data_len < 0 || data_len > UINT16_MAX) {
        return ESP_CODEC_DEV_NOT_SUPPORT;
    }
    error_t error = i2c_controller_write_register(context->i2c_controller, context->address, (uint8_t) reg, (const uint8_t*) data, (uint16_t) data_len, I2C_TIMEOUT_TICKS);
    return (error == ERROR_NONE) ? ESP_CODEC_DEV_OK : ESP_CODEC_DEV_WRITE_FAIL;
}

static int ctrl_close(const audio_codec_ctrl_if_t* handle) {
    struct I2cCtrlContext* context = (struct I2cCtrlContext*) handle;
    context->is_open = false;
    return ESP_CODEC_DEV_OK;
}

const audio_codec_ctrl_if_t* audio_codec_adapter_new_i2c_ctrl(struct Device* i2c_controller, uint8_t address) {
    if (i2c_controller == NULL) {
        return NULL;
    }

    struct I2cCtrlContext* context = (struct I2cCtrlContext*) calloc(1, sizeof(struct I2cCtrlContext));
    if (context == NULL) {
        return NULL;
    }

    context->i2c_controller = i2c_controller;
    context->address = address;
    context->is_open = false;
    context->base.open = ctrl_open;
    context->base.is_open = ctrl_is_open;
    context->base.read_reg = ctrl_read_reg;
    context->base.write_reg = ctrl_write_reg;
    context->base.close = ctrl_close;

    return &context->base;
}

// Note: esp_codec_dev already provides audio_codec_delete_ctrl_if() (calls ->close then frees);
// no adapter-specific cleanup is needed, so we don't redefine it here.
