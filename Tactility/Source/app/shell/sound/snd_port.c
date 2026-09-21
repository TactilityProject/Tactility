/*
 * snd_port.c - breezy_sound port for Tactility.
 *
 * Upstream ports talk to I2S and the codec directly (see the Tanmatsu example, which drives an
 * ES8156 through badge-bsp). Tactility puts an AUDIO_STREAM_TYPE device in front of both, so this
 * port is mostly a thin adapter: open an output stream at the mixer's rate, write frames to it, and
 * let the kernel own clock setup, codec configuration and the amplifier.
 *
 * The stream is opened on demand rather than at init, matching the contract in snd_port.h: the
 * output stays stopped until the first note, so an idle shell isn't holding the codec open.
 */

#include <Tactility/app/shell/sound/snd_port.h>

#include <tactility/device.h>
#include <tactility/drivers/audio_stream.h>

#include <tactility/freertos/freertos.h>
#include <tactility/log.h>

static const char* TAG = "snd_port";

/* The mixer renders at 44100 Hz; audio_stream resamples if the codec wants something else. */
#define SAMPLE_RATE 44100
#define BITS_PER_SAMPLE 16
#define CHANNELS 2

/* Bounded so a wedged codec surfaces as dropped audio rather than a hung mixer task. */
#define WRITE_TIMEOUT pdMS_TO_TICKS(1000)

/*
 * limit_peak: full scale. Unlike the Tanmatsu port - which lowers the ceiling to suit a small
 * speaker - output here goes through the codec's own shared volume control, which the user governs
 * from Settings, so the mixer should hand over an undistorted full-range signal and let that decide
 * how loud things actually get.
 */
const snd_port_desc_t snd_port_desc = {
    .stereo     = true,
    .limit_peak = 32767,
};

static struct Device* g_device = NULL;
static AudioStreamHandle g_stream = NULL;

error_t snd_port_init(void)
{
    if (device_get_first_active_by_type(&AUDIO_STREAM_TYPE, &g_device) != ERROR_NONE) {
        LOG_E(TAG, "No audio stream device");
        return ERROR_NOT_FOUND;
    }

    bool supported = false;
    if (audio_stream_is_supported(g_device, AUDIO_CODEC_DIR_OUTPUT, &supported) != ERROR_NONE ||
        !supported) {
        LOG_E(TAG, "Device has no output codec");
        device_put(g_device);
        g_device = NULL;
        return ERROR_NOT_SUPPORTED;
    }

    /* Leave the output stopped; snd_port_start() opens the stream on the first note. */
    return ERROR_NONE;
}

void snd_port_start(void)
{
    if (g_device == NULL || g_stream != NULL) {
        return;
    }

    const struct AudioStreamConfig config = {
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = BITS_PER_SAMPLE,
        .channels = CHANNELS,
    };

    if (audio_stream_open_output(g_device, &config, &g_stream) != ERROR_NONE) {
        LOG_W(TAG, "Failed to open output stream");
        g_stream = NULL;
    }
}

void snd_port_stop(void)
{
    if (g_stream != NULL) {
        audio_stream_close(g_stream);
        g_stream = NULL;
    }
}

void snd_port_write(const int16_t* frames, int nframes)
{
    if (g_stream == NULL || nframes <= 0) {
        return;
    }

    /*
     * snd_port.h specifies that this blocks until the output has room - that back-pressure is what
     * paces the mixer task, so no extra rate limiting is needed here. A timeout still applies so a
     * stalled codec can't wedge the task permanently.
     */
    const size_t bytes = (size_t)nframes * CHANNELS * sizeof(int16_t);
    size_t written = 0;

    const error_t result = audio_stream_write(g_stream, frames, bytes, &written, WRITE_TIMEOUT);
    if (result != ERROR_NONE) {
        /*
         * ERROR_INVALID_STATE/ERROR_RESOURCE here typically mean a hotplug codec change (e.g.
         * USB audio attach/detach) re-bound the output direction to a different codec device -
         * g_stream is now permanently dead. Close and reopen against whatever codec is current
         * instead of silently dropping audio forever.
         */
        LOG_W(TAG, "Audio write failed (%d) - reopening", (int)result);
        audio_stream_close(g_stream);
        g_stream = NULL;
        snd_port_start();
    }
}

/* Releases the device reference taken in snd_port_init(). Called when the app shuts down. */
void snd_port_deinit(void)
{
    snd_port_stop();
    if (g_device != NULL) {
        device_put(g_device);
        g_device = NULL;
    }
}
