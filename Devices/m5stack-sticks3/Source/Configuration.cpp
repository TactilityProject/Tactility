#include "devices/Display.h"
#include "devices/Power.h"

#include <tactility/device.h>
#include <drivers/m5pm1.h>

#include <Tactility/hal/Configuration.h>
#include <ButtonControl.h>
#include <PwmBacklight.h>

using namespace tt::hal;

static constexpr auto* TAG = "StickS3";

// Audio codec register programming is owned by the es8311-module driver (registered as an
// AUDIO_CODEC_TYPE device, see m5stack,sticks3.dts). This board still needs to drive the
// AW8737 speaker amplifier's enable line via m5pm1 (PM1_G3), which is board wiring glue
// rather than codec configuration, so it stays here.
static void enableSpeakerAmplifier() {
    auto* m5pm1 = device_find_by_name("m5pm1");
    if (m5pm1 == nullptr) {
        LOG_W(TAG, "m5pm1 not found -- speaker amp not enabled");
        return;
    }

    error_t error = m5pm1_set_speaker_enable(m5pm1, true);
    if (error != ERROR_NONE) {
        LOG_E(TAG, "Failed to enable speaker amplifier: %s", error_to_string(error));
    }
}

bool initBoot() {
    enableSpeakerAmplifier();
    return driver::pwmbacklight::init(GPIO_NUM_38, 512);
}

static DeviceVector createDevices() {
    return {
        createPower(),
        ButtonControl::createTwoButtonControl(11, 12), // top button, side button
        createDisplay(),
    };
}

extern const Configuration hardwareConfiguration = {
    .initBoot = initBoot,
    .createDevices = createDevices
};
