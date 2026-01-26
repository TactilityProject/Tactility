#include "TrackballDevice.h"
#include <Trackball/Trackball.h>  // Driver
#include <Tactility/Logger.h>
#include <Tactility/lvgl/LvglSync.h>
#include <Tactility/settings/TrackballSettings.h>

static const auto LOGGER = tt::Logger("TrackballDevice");

bool TrackballDevice::start() {
    if (initialized) {
        return true;
    }

    // T-Deck trackball GPIO configuration from LilyGo reference
    trackball::TrackballConfig config = {
        .pinRight = GPIO_NUM_2,       // BOARD_TBOX_G02
        .pinUp = GPIO_NUM_3,          // BOARD_TBOX_G01
        .pinLeft = GPIO_NUM_1,        // BOARD_TBOX_G04
        .pinDown = GPIO_NUM_15,       // BOARD_TBOX_G03
        .pinClick = GPIO_NUM_0,       // BOARD_BOOT_PIN
        .encoderSensitivity = 1,      // 1 step per tick for menu navigation
        .pointerSensitivity = 10      // 10 pixels per tick for cursor movement
    };

    indev = trackball::init(config);
    if (indev == nullptr) {
        return false;
    }

    initialized = true;

    // Apply persisted trackball settings (requires LVGL lock for cursor manipulation)
    auto tbSettings = tt::settings::trackball::loadOrGetDefault();
    if (tt::lvgl::lock(100)) {
        trackball::setMode(tbSettings.trackballMode == tt::settings::trackball::TrackballMode::Pointer
            ? trackball::Mode::Pointer
            : trackball::Mode::Encoder);
        trackball::setEncoderSensitivity(tbSettings.encoderSensitivity);
        trackball::setPointerSensitivity(tbSettings.pointerSensitivity);
        trackball::setEnabled(tbSettings.trackballEnabled);
        tt::lvgl::unlock();
    } else {
        LOGGER.warn("Failed to acquire LVGL lock for trackball settings");
    }

    return true;
}

bool TrackballDevice::stop() {
    if (initialized) {
        // LVGL will handle indev cleanup
        trackball::deinit();
        indev = nullptr;
        initialized = false;
    }
    return true;
}
