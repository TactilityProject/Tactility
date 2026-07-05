#include "Cst66xxTouch.h"

#include <tactility/log.h>
#include <Tactility/app/App.h>
#include <tactility/drivers/gpio_controller.h>
#include <tactility/drivers/i2c_controller.h>

#include <freertos/FreeRTOS.h>

// Touch reset is wired to XL9555 P07 (active low).
static constexpr uint32_t XL9555_PIN_TOUCH_RST = 7;

constexpr auto* TAG = "CST66xx";

static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(20);

// The protocol uses 4-byte register addresses, taken from the vendor's
// lib/HynTouch/src/hyn_cst66xx.c.

// Normal-mode setup sequence (cst66xx_set_workmode, NOMAL_MODE).
static constexpr uint8_t CMD_DISABLE_LP_I2C[4] = {0xD0, 0x00, 0x04, 0x00};
static constexpr uint8_t CMD_NORMAL_A[4] = {0xD0, 0x00, 0x00, 0x00};
static constexpr uint8_t CMD_NORMAL_B[4] = {0xD0, 0x00, 0x0C, 0x00};
static constexpr uint8_t CMD_NORMAL_C[4] = {0xD0, 0x00, 0x01, 0x00};
// Read-point register: write these 4 bytes, then read the touch frame.
static constexpr uint8_t REG_READ_POINT[4] = {0xD0, 0x07, 0x00, 0x00};
// Acknowledge/clear after each read.
static constexpr uint8_t CMD_ACK[4] = {0xD0, 0x00, 0x02, 0xAB};
// Identity/config register (cst66xx_updata_tpinfo): read 50 bytes; buf[2]==0xCA
// && buf[3]==0xCA confirms a CST66xx.
static constexpr uint8_t REG_INFO[4] = {0xD0, 0x03, 0x00, 0x00};

static void setNormalMode(::Device* i2c, uint8_t address) {
    i2c_controller_write(i2c, address, CMD_DISABLE_LP_I2C, sizeof(CMD_DISABLE_LP_I2C), I2C_TIMEOUT);
    vTaskDelay(pdMS_TO_TICKS(1));
    i2c_controller_write(i2c, address, CMD_DISABLE_LP_I2C, sizeof(CMD_DISABLE_LP_I2C), I2C_TIMEOUT);
    i2c_controller_write(i2c, address, CMD_NORMAL_A, sizeof(CMD_NORMAL_A), I2C_TIMEOUT);
    i2c_controller_write(i2c, address, CMD_NORMAL_B, sizeof(CMD_NORMAL_B), I2C_TIMEOUT);
    i2c_controller_write(i2c, address, CMD_NORMAL_C, sizeof(CMD_NORMAL_C), I2C_TIMEOUT);
}

bool Cst66xxTouch::start() {
    // Reset the controller (XL9555 P07, active low). The chip only re-initialises
    // into normal reporting mode after a clean reset pulse.
    auto* xl9555 = device_find_by_name("xl9555");
    if (xl9555 != nullptr) {
        auto* rst = gpio_descriptor_acquire(xl9555, XL9555_PIN_TOUCH_RST, GPIO_OWNER_GPIO);
        if (rst != nullptr) {
            gpio_descriptor_set_flags(rst, GPIO_FLAG_DIRECTION_OUTPUT);
            gpio_descriptor_set_level(rst, false);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_descriptor_set_level(rst, true);
            gpio_descriptor_release(rst);
        }
    }
    // The reset pulse exits boot mode; give the controller time to re-init.
    vTaskDelay(pdMS_TO_TICKS(80));

    // Put the controller into normal reporting mode and verify the identity. The
    // first read after reset can miss, so retry like the vendor's
    // cst66xx_updata_tpinfo (read 0xD0030000, expect buf[2]==buf[3]==0xCA).
    auto* i2c = configuration.i2cController;
    const uint8_t address = configuration.address;
    bool confirmed = false;
    for (int attempt = 0; attempt < 5 && !confirmed; ++attempt) {
        setNormalMode(i2c, address);
        uint8_t info[50] = {};
        if (i2c_controller_write(i2c, address, REG_INFO, sizeof(REG_INFO), I2C_TIMEOUT) == ERROR_NONE &&
            i2c_controller_read(i2c, address, info, sizeof(info), I2C_TIMEOUT) == ERROR_NONE &&
            info[2] == 0xCA && info[3] == 0xCA) {
            confirmed = true;
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (confirmed) {
        LOG_I(TAG, "CST66xx initialised");
    } else {
        LOG_W(TAG, "CST66xx identity not confirmed (continuing)");
    }
    return true;
}

bool Cst66xxTouch::stop() {
    return true;
}

bool Cst66xxTouch::readPoint(int16_t& x, int16_t& y) {
    // CST66xx frame (cst66xx_report): write the read-point register, then read 9
    // bytes. buf[2]=report type (0xFF=position/key), buf[3] low nibble = finger
    // count, high nibble = key count. The first 5-byte slot (buf[4..8]) is a key
    // slot when key count > 0, otherwise the first finger; further fingers, if
    // any, follow at buf[index+...].
    uint8_t buf[9] = {};
    error_t err = i2c_controller_write(configuration.i2cController, configuration.address, REG_READ_POINT, sizeof(REG_READ_POINT), I2C_TIMEOUT);
    if (err == ERROR_NONE) {
        err = i2c_controller_read(configuration.i2cController, configuration.address, buf, sizeof(buf), I2C_TIMEOUT);
    }
    if (err != ERROR_NONE) {
        return false;
    }

    // Acknowledge/clear the frame after every read.
    i2c_controller_write(configuration.i2cController, configuration.address, CMD_ACK, sizeof(CMD_ACK), I2C_TIMEOUT);

    const uint8_t reportType = buf[2];
    const uint8_t fingerCount = buf[3] & 0x0F;
    const uint8_t keyCount = (buf[3] & 0xF0) >> 4;

    // Bezel touch-keys are reported in the first slot (buf[8]): low nibble = key
    // id, high nibble = state (non-zero = pressed). The controller reports keys
    // mutually exclusively with finger coordinates, so a key frame never carries
    // a touch point.
    if (reportType == 0xFF && keyCount > 0) {
        const uint8_t keyByte = buf[8];
        handleBezelKey(keyByte & 0x0F, (keyByte >> 4) != 0);
        return false;
    }
    // No key in this frame: clear the latch so the next press fires once.
    bezelKeyDown = false;

    if (reportType != 0xFF || fingerCount < 1) {
        return false;
    }

    // First finger's slot follows any key slots.
    const uint8_t index = keyCount * 5;
    const uint8_t event = buf[index + 8] >> 4; // 0 = up
    if (event == 0) {
        return false;
    }

    int16_t rawX = static_cast<int16_t>(buf[index + 4] | (static_cast<uint16_t>(buf[index + 7] & 0x0F) << 8));
    int16_t rawY = static_cast<int16_t>(buf[index + 5] | (static_cast<uint16_t>(buf[index + 7] & 0xF0) << 4));

    if (configuration.swapXy) {
        std::swap(rawX, rawY);
    }
    if (configuration.mirrorX) {
        rawX = static_cast<int16_t>(configuration.width - 1 - rawX);
    }
    if (configuration.mirrorY) {
        rawY = static_cast<int16_t>(configuration.height - 1 - rawY);
    }

    x = rawX;
    y = rawY;
    return true;
}

// Bezel touch-key ids as reported by the CST66xx, left to right (confirmed on
// hardware). To re-map a button, change the action in the switch below — the
// mapping is also documented in NOTES.md.
static constexpr uint8_t BEZEL_KEY_HEART = 0;    // left
static constexpr uint8_t BEZEL_KEY_SPEECH = 1;   // centre
static constexpr uint8_t BEZEL_KEY_AIRPLANE = 2; // right

void Cst66xxTouch::handleBezelKey(uint8_t keyId, bool pressed) {
    if (!pressed) {
        bezelKeyDown = false;
        return;
    }
    if (bezelKeyDown) {
        return; // still held; the press edge was already handled
    }
    bezelKeyDown = true;

    LOG_D(TAG, "bezel key %u", keyId);

    // Bezel button -> navigation action. Edit these cases to customise:
    switch (keyId) {
        case BEZEL_KEY_HEART: // Back: stop the current app (no-op at the launcher root)
            tt::app::stop();
            break;
        case BEZEL_KEY_SPEECH: // Home: the launcher
            tt::app::start("Launcher");
            break;
        case BEZEL_KEY_AIRPLANE: // Recents: the app switcher
            tt::app::start("AppList");
            break;
        default:
            break;
    }
}

void Cst66xxTouch::readCallback(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<Cst66xxTouch*>(lv_indev_get_user_data(indev));

    int16_t x;
    int16_t y;
    if (self->readPoint(x, y)) {
        self->lastX = x;
        self->lastY = y;
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        // Report the release at the last known position.
        data->point.x = self->lastX;
        data->point.y = self->lastY;
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool Cst66xxTouch::startLvgl(lv_display_t* display) {
    if (indev != nullptr) {
        return true;
    }

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, &readCallback);
    lv_indev_set_display(indev, display);
    lv_indev_set_user_data(indev, this);
    return true;
}

bool Cst66xxTouch::stopLvgl() {
    if (indev == nullptr) {
        return true;
    }
    lv_indev_delete(indev);
    indev = nullptr;
    return true;
}
