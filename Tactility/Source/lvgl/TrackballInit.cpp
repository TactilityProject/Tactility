#include <tactility/device.h>
#include <tactility/drivers/trackball.h>

#include <Tactility/settings/TrackballSettings.h>

namespace tt::lvgl {

void initTrackball() {
    TrackballSettings settings;
    // Without stored settings, the devices keep the defaults of their devicetree
    if (!settings::trackball::load(settings)) {
        return;
    }
    // Replacing the child device takes the device ledger lock, so this can't run inside device_for_each_of_type()
    Device* trackball = nullptr;
    if (device_get_first_active_by_type(&TRACKBALL_TYPE, &trackball) != ERROR_NONE) {
        return;
    }
    trackball_set_settings(trackball, &settings);
    device_put(trackball);
}

}
