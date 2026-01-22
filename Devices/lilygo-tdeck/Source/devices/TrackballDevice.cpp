#include "TrackballDevice.h"
#include <Trackball/Trackball.h>  // Driver

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
    if (indev != nullptr) {
        initialized = true;
        return true;
    }

    return false;
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
