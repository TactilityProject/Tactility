/*
 * snd_core.c - 8-voice PICO-8-flavored synth mixer + PCM stream.
 *
 * One background task renders all voices into 256-frame mono chunks, sums in
 * the PCM stream, and hands the result to the board's snd_port (snd_port.h)
 * for I2S output at 44.1 kHz s16. Power handling: after all sources go
 * silent, feed a few chunks of silence to drain the DMA queue, then stop the
 * port; wake on the next note via task notification.
 *
 * All synthesis is fixed point: 32-bit phase accumulators, no floats in the
 * render loop (frequency setup uses float once per note_on, which is fine).
 */

#include <Tactility/app/shell/sound/snd_core.h>
#include <Tactility/app/shell/sound/snd_p8.h>
#include <Tactility/app/shell/sound/snd_port.h>
#include <Tactility/app/shell/sound/snd_osc.h>

#include <math.h>
#include <string.h>

#include <tactility/concurrent/mutex.h>
#include <tactility/freertos/freertos.h>
#include <tactility/freertos/task.h>
#include <tactility/log.h>

static const char *TAG = "snd";

#define SND_RATE          44100
#define CHUNK_FRAMES      256                  /* ~5.8 ms */
#define DRAIN_CHUNKS      8                    /* ~46 ms of silence before power-down */
#define ATTACK_STEP       (32767 / (SND_RATE * 5 / 1000))    /* ~5 ms to full */
#define RELEASE_STEP      (32767 / (SND_RATE * 80 / 1000))   /* ~80 ms fade */

enum env_state { ENV_OFF = 0, ENV_ATTACK, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    volatile uint8_t env_state;
    uint8_t  wave;
    uint8_t  vol;          /* 0-255 */
    uint32_t phase_inc;    /* freq * 2^32 / SND_RATE */
    osc_t    osc;          /* shared waveform core (snd_osc.h) */
    int32_t  env;          /* Q15 envelope level */
    uint32_t seq;          /* allocation age, for voice stealing */
} voice_t;

static voice_t      g_voices[SND_NUM_VOICES];
static struct Mutex g_lock;
static TaskHandle_t g_task = NULL;
static uint32_t     g_seq = 0;
static bool         g_powered = false;  /* ports leave output stopped after init */

static int32_t g_volume = 100;   /* master volume, percent, 0..100 */
static int32_t g_gain = 65536;   /* limiter gain, Q16, <= 1.0 */
static int32_t g_env  = 0;       /* limiter peak-hold envelope */
static int16_t g_mono[CHUNK_FRAMES];
static int16_t g_out[CHUNK_FRAMES * 2];

/* PCM stream: SPSC ring of stereo frames. The writer (app) copies frames in
 * and advances g_str_head; the mixer task consumes and advances g_str_tail.
 * Both are free-running uint32 counters (indices mod STREAM_FRAMES). */
#define STREAM_FRAMES 4096              /* ~93 ms at 44.1 kHz */
static int16_t  g_stream_buf[STREAM_FRAMES * 2];
static volatile uint32_t g_str_head = 0;
static volatile uint32_t g_str_tail = 0;
static volatile bool     g_stream_on = false;
static int      g_stream_ch = 2;

/* Waveform synthesis lives in snd_osc.h (shared with the snd_p8 tracker);
 * relative wave loudness follows zepto8's measured PICO-8 amplitudes. */

/* Master volume on the final combined bus (synth + PCM stream): linear
 * 0..100%, plus a safety clamp at limit_peak -- the sum of two
 * limit_peak-bounded sources can briefly exceed it. */
static inline int32_t apply_master(int32_t s)
{
    const int32_t limit_peak = snd_port_desc.limit_peak;
    int32_t y = s * g_volume / 100;
    if (y > limit_peak) y = limit_peak;
    else if (y < -limit_peak) y = -limit_peak;
    return y;
}

/* Render one mono chunk. Returns the number of active voices. */
static int render_chunk(void)
{
    int active = 0;
    int32_t mix[CHUNK_FRAMES];
    memset(mix, 0, sizeof(mix));

    for (int vi = 0; vi < SND_NUM_VOICES; vi++) {
        voice_t *v = &g_voices[vi];
        if (v->env_state == ENV_OFF) continue;
        active++;

        for (int i = 0; i < CHUNK_FRAMES; i++) {
            switch (v->env_state) {
                case ENV_ATTACK:
                    v->env += ATTACK_STEP;
                    if (v->env >= 32767) { v->env = 32767; v->env_state = ENV_SUSTAIN; }
                    break;
                case ENV_RELEASE:
                    v->env -= RELEASE_STEP;
                    if (v->env <= 0) { v->env = 0; v->env_state = ENV_OFF; }
                    break;
                default:
                    break;
            }
            if (v->env_state == ENV_OFF) break;

            int32_t s = osc_sample(&v->osc, v->wave, v->phase_inc,
                                   SND_OSC_NOISE_C_44100);   /* +/-16384 max */
            s = (s * v->env) >> 15;               /* envelope */
            s = (s * v->vol) >> 8;                /* per-voice volume */
            mix[i] += s;

            osc_advance(&v->osc, v->phase_inc);
        }
    }

    /* PICO-8 tracker (snd_p8.c) renders into the same pre-limiter bus. */
    active += snd_p8_mix(mix, CHUNK_FRAMES);

    /* Synth limiter, bounding the bus to limit_peak (the speaker's clean
     * ceiling, per board -- not int16 range). The target gain follows a
     * peak-hold envelope (instant rise, slow decay) rather than the raw chunk
     * peak, so beating chord peaks don't pump it audibly. The applied gain
     * then slews toward the target per sample -- a gain step at a chunk
     * boundary is an audible pop -- fast on the way down (~0.4 ms; the soft
     * knee below absorbs the brief overshoot), slow on the way up. */
    const int32_t limit_peak = snd_port_desc.limit_peak;
    int32_t peak = 0;
    for (int i = 0; i < CHUNK_FRAMES; i++) {
        int32_t a = mix[i] < 0 ? -mix[i] : mix[i];
        if (a > peak) peak = a;
    }
    g_env -= g_env >> 8;                            /* slow fall */
    if (peak > g_env) g_env = peak;                 /* instant rise */
    const int32_t target = (g_env > limit_peak)
                   ? (int32_t)(((int64_t)limit_peak << 16) / g_env)
                   : 65536;                         /* Q16, <= 1.0 */
    int32_t gain = g_gain;
    for (int i = 0; i < CHUNK_FRAMES; i++) {
        int32_t d = target - gain;
        gain += (d < 0) ? (d >> 4)                  /* attack, ~0.4 ms */
                        : (d >> 13) + (d > 0);      /* release, ~190 ms */
        int32_t s = (int32_t)(((int64_t)mix[i] * gain) >> 16);
        if (s > limit_peak)       s = limit_peak + (s - limit_peak) / 4;
        else if (s < -limit_peak) s = -limit_peak + (s + limit_peak) / 4;
        if (s > INT16_MAX) s = INT16_MAX;
        else if (s < INT16_MIN) s = INT16_MIN;
        g_mono[i] = (int16_t)s;
    }
    g_gain = gain;
    return active;
}

static void snd_task(void *arg)
{
    (void)arg;
    int silence_chunks = 0;

    for (;;) {
        int active = render_chunk();
        if (g_stream_on) active++;   /* open stream holds power up, even on underrun */

        if (active == 0) {
            if (!g_powered) {
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                continue;
            }
            if (silence_chunks >= DRAIN_CHUNKS) {
                snd_port_stop();
                g_powered = false;
                silence_chunks = 0;
                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                continue;
            }
            memset(g_mono, 0, sizeof(g_mono));
            silence_chunks++;
        } else {
            if (!g_powered) {
                snd_port_start();
                g_powered = true;
            }
            silence_chunks = 0;
        }

        /* Output stage: sum the PCM stream on top of the synth, scaling it
         * from int16 full scale down to limit_peak so both sources target the
         * speaker's clean ceiling. The stream is mixed post-limiter: mixing
         * it pre-limiter would make synth peaks duck the music. Master volume
         * then applies once to the combined bus. Stream underrun contributes
         * silence, never blocking on the app; the port's write blocks until
         * DMA has room -- that paces this task to the sample rate. */
        const int32_t limit_peak = snd_port_desc.limit_peak;
        uint32_t avail = g_stream_on ? g_str_head - g_str_tail : 0;
        if (snd_port_desc.stereo) {
            for (int i = 0; i < CHUNK_FRAMES; i++) {
                int32_t l = g_mono[i], r = g_mono[i];
                if (avail > 0) {
                    uint32_t idx = (g_str_tail % STREAM_FRAMES) * 2;
                    l += ((int32_t)g_stream_buf[idx] * limit_peak) >> 15;
                    r += ((int32_t)g_stream_buf[idx + 1] * limit_peak) >> 15;
                    g_str_tail = g_str_tail + 1;
                    avail--;
                }
                g_out[i * 2]     = (int16_t)apply_master(l);
                g_out[i * 2 + 1] = (int16_t)apply_master(r);
            }
            snd_port_write(g_out, CHUNK_FRAMES);
        } else {
            for (int i = 0; i < CHUNK_FRAMES; i++) {
                int32_t s = g_mono[i];
                if (avail > 0) {         /* mono out: downmix stream to (L+R)/2 */
                    uint32_t idx = (g_str_tail % STREAM_FRAMES) * 2;
                    int32_t m = ((int32_t)g_stream_buf[idx] + g_stream_buf[idx + 1]) / 2;
                    s += (m * limit_peak) >> 15;
                    g_str_tail = g_str_tail + 1;
                    avail--;
                }
                g_mono[i] = (int16_t)apply_master(s);
            }
            snd_port_write(g_mono, CHUNK_FRAMES);
        }
    }
}

error_t snd_init(void)
{
    if (g_task != NULL) return ERROR_NONE;

    mutex_construct(&g_lock);

    error_t err = snd_port_init();
    if (err != ERROR_NONE) {
        LOG_E(TAG, "snd_port_init failed: %s", error_to_string(err));
        return err;
    }

    /*
     * Pinned to core 0 at a high priority (matching DoomAudio's mixer task): USB Audio pulls
     * from its ring buffer on a fixed real-time schedule regardless of what the app is doing,
     * and silently drops audio if the ring runs dry. At the default priority/no pinning, other
     * app-level work could delay this task's next mix+write long enough to starve the ring -
     * audible as playback "cutting in and out" when output is a USB device.
     *
     * Core pinning is an ESP-IDF SMP extension with no equivalent on the single-core POSIX port.
     */
#ifdef ESP_PLATFORM
    const BaseType_t created = xTaskCreatePinnedToCore(snd_task, "snd_mixer", 4096, NULL, 10, &g_task, 0);
#else
    const BaseType_t created = xTaskCreate(snd_task, "snd_mixer", 4096, NULL, 10, &g_task);
#endif
    if (created != pdPASS) {
        LOG_E(TAG, "Failed to create mixer task");
        return ERROR_OUT_OF_MEMORY;
    }
    LOG_I(TAG, "Mixer started (%d voices, %d Hz)", SND_NUM_VOICES, SND_RATE);
    return ERROR_NONE;
}

int snd_note_on(int voice, float freq_hz, int wave, int vol)
{
    if (g_task == NULL) return -1;
    if (freq_hz < 20.0f) freq_hz = 20.0f;
    if (freq_hz > 12000.0f) freq_hz = 12000.0f;
    if (wave < 0 || wave >= SND_WAVE_COUNT) wave = SND_WAVE_TRIANGLE;
    if (vol < 0) vol = 0;
    if (vol > 255) vol = 255;

    mutex_lock(&g_lock);
    if (voice < 0) {
        /* Prefer a free voice; otherwise steal the oldest. */
        uint32_t oldest = UINT32_MAX;
        int steal = 0;
        for (int i = 0; i < SND_NUM_VOICES; i++) {
            if (g_voices[i].env_state == ENV_OFF) { voice = i; break; }
            if (g_voices[i].seq < oldest) { oldest = g_voices[i].seq; steal = i; }
        }
        if (voice < 0) voice = steal;
    } else if (voice >= SND_NUM_VOICES) {
        mutex_unlock(&g_lock);
        return -1;
    }

    voice_t *v = &g_voices[voice];
    v->wave      = (uint8_t)wave;
    v->vol       = (uint8_t)vol;
    v->phase_inc = (uint32_t)(freq_hz * 4294967296.0f / SND_RATE);
    osc_reset(&v->osc, 0x12345678u + (uint32_t)voice);
    /* Noise key-tracking gain wants the PICO-8 key for this frequency:
     * key = 12 * log2(freq / 65.406). Setup-only float, like phase_inc. */
    osc_set_key(&v->osc, (int)(logf(freq_hz * (1.0f / 65.406f))
                               * 17.3123f + 0.5f));
    v->env       = 0;
    v->seq       = ++g_seq;
    v->env_state = ENV_ATTACK;
    mutex_unlock(&g_lock);

    xTaskNotifyGive(g_task);
    return voice;
}

void snd_note_off(int voice)
{
    if (g_task == NULL || voice < 0 || voice >= SND_NUM_VOICES) return;
    mutex_lock(&g_lock);
    if (g_voices[voice].env_state == ENV_ATTACK ||
        g_voices[voice].env_state == ENV_SUSTAIN) {
        g_voices[voice].env_state = ENV_RELEASE;
    }
    mutex_unlock(&g_lock);
}

void snd_set_volume(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    g_volume = pct;
}

int snd_get_volume(void)
{
    return g_volume;
}

int snd_stream_open(int rate, int channels)
{
    if (g_task == NULL || rate != SND_RATE ||
        (channels != 1 && channels != 2) || g_stream_on) {
        return -1;
    }
    g_str_head = 0;
    g_str_tail = 0;
    g_stream_ch = channels;
    g_stream_on = true;
    xTaskNotifyGive(g_task);   /* wake + power up if the mixer was idle */
    return 0;
}

int snd_stream_space(void)
{
    if (!g_stream_on) return 0;
    return (int)(STREAM_FRAMES - (g_str_head - g_str_tail));
}

int snd_stream_write(const int16_t *frames, int nframes)
{
    if (!g_stream_on || frames == NULL || nframes < 0) return -1;
    int space = (int)(STREAM_FRAMES - (g_str_head - g_str_tail));
    if (nframes > space) nframes = space;
    uint32_t head = g_str_head;
    for (int i = 0; i < nframes; i++) {
        uint32_t idx = (head % STREAM_FRAMES) * 2;
        if (g_stream_ch == 2) {
            g_stream_buf[idx]     = frames[i * 2];
            g_stream_buf[idx + 1] = frames[i * 2 + 1];
        } else {
            g_stream_buf[idx]     = frames[i];
            g_stream_buf[idx + 1] = frames[i];
        }
        head++;
    }
    g_str_head = head;   /* publish after the copy */
    return nframes;
}

void snd_stream_close(void)
{
    g_stream_on = false;
}

/* Wake the mixer task (snd_p8.c calls this when playback starts). */
void snd_core_wake(void)
{
    if (g_task != NULL) xTaskNotifyGive(g_task);
}

void snd_all_off(void)
{
    if (g_task == NULL) return;
    snd_stream_close();
    snd_p8_stop();
    mutex_lock(&g_lock);
    for (int i = 0; i < SND_NUM_VOICES; i++) {
        if (g_voices[i].env_state == ENV_ATTACK ||
            g_voices[i].env_state == ENV_SUSTAIN) {
            g_voices[i].env_state = ENV_RELEASE;
        }
    }
    mutex_unlock(&g_lock);
}
