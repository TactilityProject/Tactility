#include "KeyboardBacklight.h"

#include <cstring>
#include <esp_log.h>
#include <tactility/log.h>

constexpr auto* TAG = "KeyboardBacklight";

namespace keyboardbacklight {

static const uint8_t CMD_BRIGHTNESS = 0x01;
static const uint8_t CMD_DEFAULT_BRIGHTNESS = 0x02;

static i2c_port_t g_i2cPort = I2C_NUM_MAX;
static uint8_t g_slaveAddress = 0x55;
static uint8_t g_currentBrightness = 127;

// TODO: Umm...something. Calls xxxBrightness, ignores return values.
bool init(i2c_port_t i2cPort, uint8_t slaveAddress) {
    g_i2cPort = i2cPort;
    g_slaveAddress = slaveAddress;
    
    LOG_I(TAG, "Initialized on I2C port %d, address 0x%02X", static_cast<int>(g_i2cPort), (unsigned)g_slaveAddress);

    // Set a reasonable default brightness
    if (!setDefaultBrightness(127)) {
        LOG_E(TAG, "Failed to set default brightness");
        return false;
    }

    if (!setBrightness(127)) {
        LOG_E(TAG, "Failed to set brightness");
        return false;
    }
    
    return true;
}

bool setBrightness(uint8_t brightness) {
    if (g_i2cPort >= I2C_NUM_MAX) {
        LOG_E(TAG, "Not initialized");
        return false;
    }

    // Skip if brightness is already at target value (avoid I2C spam on every keypress)
    if (brightness == g_currentBrightness) {
        return true;
    }

    LOG_I(TAG, "Setting brightness to %d on I2C port %d, address 0x%02X", brightness, static_cast<int>(g_i2cPort), (unsigned)g_slaveAddress);
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_slaveAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, CMD_BRIGHTNESS, true);
    i2c_master_write_byte(cmd, brightness, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(g_i2cPort, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        g_currentBrightness = brightness;
        LOG_I(TAG, "Successfully set brightness to %d", brightness);
        return true;
    } else {
        LOG_E(TAG, "Failed to set brightness: %s (0x%02X)", esp_err_to_name(ret), (unsigned)ret);
        return false;
    }
}

bool setDefaultBrightness(uint8_t brightness) {
    if (g_i2cPort >= I2C_NUM_MAX) {
        LOG_E(TAG, "Not initialized");
        return false;
    }

    // Clamp to valid range for default brightness
    if (brightness < 30) {
        brightness = 30;
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (g_slaveAddress << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, CMD_DEFAULT_BRIGHTNESS, true);
    i2c_master_write_byte(cmd, brightness, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(g_i2cPort, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    
    if (ret == ESP_OK) {
        LOG_D(TAG, "Set default brightness to %d", brightness);
        return true;
    } else {
        LOG_E(TAG, "Failed to set default brightness: %s", esp_err_to_name(ret));
        return false;
    }
}

uint8_t getBrightness() {
    if (g_i2cPort >= I2C_NUM_MAX) {
        LOG_E(TAG, "Not initialized");
        return 0;
    }

    return g_currentBrightness;
}

}
