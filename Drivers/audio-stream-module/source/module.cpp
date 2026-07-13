// SPDX-License-Identifier: Apache-2.0
#include <tactility/check.h>
#include <tactility/device.h>
#include <tactility/driver.h>
#include <tactility/log.h>
#include <tactility/module.h>

extern "C" {

extern Driver audio_stream_driver;
extern Device audio_stream_device;

static error_t start() {
    /* We crash when construct fails, because if a single driver fails to construct,
     * there is no guarantee that the previously constructed drivers can be destroyed */
    check(driver_construct_add(&audio_stream_driver) == ERROR_NONE);

    /* The audio-stream device is a software aggregate with no devicetree backing — it binds
     * to whichever AUDIO_CODEC_TYPE devices are present at start_device time. Constructed
     * here (after codec drivers/devices are up) rather than from a .dts node. */
    if (device_construct_add_start(&audio_stream_device, "audio-stream") != ERROR_NONE) {
        LOG_W("AudioStream", "No audio codec available; audio-stream0 not started");
    }

    return ERROR_NONE;
}

static error_t stop() {
    if (device_is_added(&audio_stream_device)) {
        device_stop(&audio_stream_device);
        device_remove(&audio_stream_device);
        device_destruct(&audio_stream_device);
    }

    /* We crash when destruct fails, because if a single driver fails to destruct,
     * there is no guarantee that the previously destroyed drivers can be recovered */
    check(driver_remove_destruct(&audio_stream_driver) == ERROR_NONE);
    return ERROR_NONE;
}

extern const ModuleSymbol audio_stream_module_symbols[];

Module audio_stream_module = {
    .name = "audio-stream",
    .start = start,
    .stop = stop,
    .symbols = audio_stream_module_symbols,
    .internal = nullptr
};

}
