/*
 * snd_p8.c - PICO-8 sfx/music tracker (see snd_p8.h).
 *
 * Fixed-point port of zepto8's player (src/pico8/sfx.cpp, WTFPL), whose
 * tracker/effect semantics and constants were measured against PICO-8 WAV
 * exports; the pork.p8 feature subset (no custom instruments, SFX filters,
 * or half-rate). Waveforms come from the shared oscillator core (snd_osc.h).
 *
 * Sequencing runs at PICO-8's native 22050 Hz, one note lasting 183 * speed
 * samples; the output is upsampled 2x (midpoint-interpolated) into the
 * 44100 Hz mixer bus. snd_core's mixer task pulls audio via snd_p8_mix(),
 * so it renders lock-free; the control calls below mutate channel state
 * under a short critical section, mirroring snd_core's note API.
 *
 * All synthesis and sequencing is integer; floats appear only in one-time
 * table setup.
 */
#include <Tactility/app/shell/sound/snd_p8.h>
#include <Tactility/app/shell/sound/snd_osc.h>

#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_p8_lock = portMUX_INITIALIZER_UNLOCKED;
#define P8_LOCK()   taskENTER_CRITICAL(&g_p8_lock)
#define P8_UNLOCK() taskEXIT_CRITICAL(&g_p8_lock)
#else  /* host test harness: single-threaded, no lock, stub wake */
#define P8_LOCK()
#define P8_UNLOCK()
#endif

#define NUM_CHANNELS   4
#define P8_RATE        22050
#define NOTE_TICKS(sp) (183u * (sp))    /* samples per note at 22050 Hz */

/* Packed note accessors (see snd_p8.h). */
#define NOTE_KEY(n)  ((n) & 0x3f)
#define NOTE_WAVE(n) (((n) >> 6) & 7)
#define NOTE_VOL(n)  (((n) >> 9) & 7)
#define NOTE_FX(n)   (((n) >> 12) & 7)

enum {
    FX_NONE = 0, FX_SLIDE, FX_VIBRATO, FX_DROP,
    FX_FADE_IN, FX_FADE_OUT, FX_ARP_FAST, FX_ARP_SLOW,
};

/* Music pattern flags (text .p8 encoding). */
#define PAT_BEGIN_LOOP 1
#define PAT_END_LOOP   2
#define PAT_STOP       4

/* Declick crossfade (zepto8): trigger thresholds and ~7.7 ms fade rate. */
#define JUMP_VOL_Q15   3277            /* volume step > 0.1 */
#define FADE_STEP_Q15  193             /* 130 / 22050 per sample */

/* Current synth parameters of one channel (the "what is sounding now"
 * snapshot the declick logic compares and crossfades between). */
typedef struct {
    uint32_t inc;        /* effect-adjusted phase increment */
    int32_t  vol;        /* Q15 */
    uint8_t  key, wave;
    uint8_t  is_music;
    osc_t    osc;
} psynth_t;

typedef struct {
    volatile int sfx;    /* playing sfx index, -1 = idle */
    int      sfx_music;  /* music sfx pending behind a user sfx, -1 = none */
    uint32_t note_len;   /* samples per note = 183 * speed */
    uint32_t tick;       /* sample position within the current note */
    int      note_id;    /* current note, 0..31 (may start past 31) */
    uint32_t time;       /* samples since launch, never loop-wrapped */
    uint32_t end_smp;    /* stop when time reaches this (UINT32_MAX = never) */
    int      prev_key;   /* previous note, for FX_SLIDE */
    int32_t  prev_vol;   /* Q15 */
    uint8_t  can_loop;
    uint8_t  is_music;
    psynth_t cur;        /* current params + oscillator */
    psynth_t fade;       /* declick: previous params fading out */
    int32_t  fade_q15;   /* 32768 -> 0 crossfade position */
} pchan_t;

static pchan_t g_chan[NUM_CHANNELS];

static struct {
    volatile int pattern;     /* -1 = off */
    int      count;
    uint32_t pos;             /* samples since pattern start (speed-1 base) */
    uint32_t len_smp;         /* pattern length in samples */
    int      mask;            /* channels reserved against sfx stealing */
    int32_t  fade_q24;        /* music fade volume, Q24 */
    int32_t  fade_step_q24;   /* per 22050 Hz sample */
} g_music;

/* Loaded cart data (copied; zeroed = silence). */
static snd_p8_sfx_t     g_sfx[64];
static snd_p8_pattern_t g_pat[64];
static int g_nsfx = 0, g_npat = 0;

/* key -> phase increment at 22050 Hz: 440 * 2^((key-33)/12) * 2^32 / 22050.
 * Built once from floats (setup only; the render path is all-integer). */
static uint32_t g_key_inc[64];

static void build_key_table(void)
{
    if (g_key_inc[0] != 0) return;
    float f = 65.40639f;                     /* key 0 = C1 (PICO-8 C0) */
    const float semi = 1.0594630944f;
    for (int i = 0; i < 64; i++) {
        g_key_inc[i] = (uint32_t)(f * (4294967296.0f / 22050.0f));
        f *= semi;
    }
}

/* ---- sequencer ---------------------------------------------------- */

static int sfx_speed(const snd_p8_sfx_t *sfx)
{
    return sfx->speed > 0 ? sfx->speed : 1;
}

/* Cache the forced-end position (in samples since launch); zepto8 computes
 * this every sample, we recompute on launch and when can_loop changes. */
static void chan_calc_end(pchan_t *c)
{
    const snd_p8_sfx_t *sfx = &g_sfx[c->sfx];
    int loop_range = (int)sfx->loop_end - (int)sfx->loop_start;
    int has_end = 0;
    int end_notes = 32;

    /* loop_start > 0 with loop_end == 0 marks an early end point (PICO-8
     * quirk: only honored for non-music sfx). */
    if (!c->is_music && sfx->loop_end == 0 && sfx->loop_start > 0) {
        has_end = 1;
        if (sfx->loop_start < end_notes) end_notes = sfx->loop_start;
    }
    if (loop_range <= 0 || !c->can_loop) {
        has_end = 1;
        if (!c->is_music) {
            /* Stop after the last non-silent note. */
            int last_note = 0;
            for (int n = 0; n < 32; n++)
                if (NOTE_VOL(sfx->notes[n]) > 0) last_note = n + 1;
            if (last_note < end_notes) end_notes = last_note;
        }
    }
    c->end_smp = has_end ? (uint32_t)end_notes * c->note_len : UINT32_MAX;
}

/* Start sfx n on a channel at sample position pos_smp into the sfx. */
static void launch_sfx(int n, int chan, uint32_t pos_smp, int is_music)
{
    pchan_t *c = &g_chan[chan];
    c->note_len = NOTE_TICKS((uint32_t)sfx_speed(&g_sfx[n]));
    c->note_id = (int)(pos_smp / c->note_len);
    c->tick = pos_smp % c->note_len;
    c->time = 0;
    /* PICO-8 shows no pitch slide from a leading C-2, so prev_key defaults
     * to 24 (zepto8). No default previous volume. */
    c->prev_key = 24;
    c->prev_vol = 0;
    c->can_loop = 1;
    c->is_music = (uint8_t)is_music;
    c->sfx = n;
    chan_calc_end(c);
}

static void set_music_pattern(int pattern)
{
    for (int i = 0; i < NUM_CHANNELS; i++)
        if (g_chan[i].is_music) {
            g_chan[i].sfx = -1;
            g_chan[i].sfx_music = -1;
        }

    if (pattern < 0 || pattern > 63) {
        g_music.pattern = -1;
        g_music.count = -1;
        g_music.pos = 0;
        g_music.mask = 0;
        g_music.len_smp = 0;
        return;
    }

    /* Pattern length: the first non-looping channel's length, else the
     * slowest looping channel's 32 notes. In samples (183 per speed-1
     * note unit). */
    const snd_p8_pattern_t *pat = &g_pat[pattern];
    int notes_looping = -1, notes_no_loop = -1;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (pat->chan[i] & 0x40) continue;
        const snd_p8_sfx_t *sfx = &g_sfx[pat->chan[i] & 0x3f];
        int speed = sfx_speed(sfx);
        if (sfx->loop_end > 0 && sfx->loop_end > sfx->loop_start) {
            int d = 32 * speed;
            if (d > notes_looping) notes_looping = d;
        } else {
            int end_notes = 32;
            if (sfx->loop_end == 0 && sfx->loop_start > 0
                && sfx->loop_start < end_notes)
                end_notes = sfx->loop_start;
            notes_no_loop = end_notes * speed;
            break;
        }
    }
    int notes = notes_no_loop > 0 ? notes_no_loop : notes_looping;
    if (notes <= 0) {
        set_music_pattern(-1);
        return;
    }

    g_music.pattern = pattern;
    g_music.pos = 0;
    g_music.len_smp = (uint32_t)notes * 183u;

    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (pat->chan[i] & 0x40) continue;
        if (g_chan[i].sfx == -1)
            launch_sfx(pat->chan[i] & 0x3f, i, 0, 1);
        else
            /* A user sfx owns the channel; queue the music sfx to be
             * picked up (mid-pattern) when it ends. */
            g_chan[i].sfx_music = pat->chan[i] & 0x3f;
    }
}

/* Per-sample music sequencing: advance the master position, handle fades
 * and pattern transitions, let idle channels pick up pending music sfx. */
static void update_music(void)
{
    if (g_music.pattern != -1) {
        g_music.pos++;
        g_music.fade_q24 += g_music.fade_step_q24;
        if (g_music.fade_q24 < 0) g_music.fade_q24 = 0;
        if (g_music.fade_q24 > (1 << 24)) g_music.fade_q24 = 1 << 24;

        if (g_music.fade_step_q24 < 0 && g_music.fade_q24 <= 0) {
            set_music_pattern(-1);
        } else if (g_music.pos >= g_music.len_smp) {
            int next = g_music.pattern + 1;
            int flags = g_pat[g_music.pattern].flags;
            if (flags & PAT_STOP) {
                next = -1;
            } else if (flags & PAT_END_LOOP) {
                while (--next > 0 && !(g_pat[next].flags & PAT_BEGIN_LOOP))
                    ;
            }
            g_music.count++;
            set_music_pattern(next);
        }
    }

    for (int i = 0; i < NUM_CHANNELS; i++) {
        pchan_t *c = &g_chan[i];
        if (c->sfx != -1 || c->sfx_music == -1) continue;
        const snd_p8_sfx_t *sfx = &g_sfx[c->sfx_music];
        uint32_t note_len = NOTE_TICKS((uint32_t)sfx_speed(sfx));
        /* Resume position in samples equals the master position (speed
         * cancels: note units scale by 1/speed, note length by speed). */
        uint32_t pos = g_music.pos;
        int loop_range = (int)sfx->loop_end - (int)sfx->loop_start;
        int want_play = 1;
        if (loop_range > 0) {
            uint32_t ls = sfx->loop_start * note_len;
            uint32_t range = (uint32_t)loop_range * note_len;
            if (pos > ls) pos = ls + (pos - ls) % range;
        } else if (pos > 32u * note_len) {
            want_play = 0;
        }
        if (want_play)
            launch_sfx(c->sfx_music, i, pos, 1);
        c->sfx_music = -1;
    }
}

/* ---- per-sample channel rendering ---------------------------------- */

static inline int32_t psynth_sample(psynth_t *p, int advance)
{
    int32_t s = osc_sample(&p->osc, p->wave, p->inc, SND_OSC_NOISE_C_22050);
    if (advance) osc_advance(&p->osc, p->inc);
    int32_t v = (int32_t)(((int64_t)s * p->vol) >> 15);
    if (p->is_music) v = (int32_t)(((int64_t)v * (g_music.fade_q24 >> 9)) >> 15);
    if (v > 32767) v = 32767;
    else if (v < -32767) v = -32767;
    return v;
}

/* Advance one channel by one 22050 Hz sample and return its output. */
static int32_t chan_render(pchan_t *c)
{
    /* New params default to "same wave/key, silent" (matches zepto8's
     * ns = last; volume = 0). */
    uint32_t inc = c->cur.inc;
    int32_t  vol = 0;
    int      key = c->cur.key;
    int      wave = c->cur.wave;
    int      is_music = c->cur.is_music;
    int      sn = c->sfx;   /* snapshot: another task may clear it mid-render */

    if (sn >= 0 && c->note_id < 32) {
        const snd_p8_sfx_t *sfx = &g_sfx[sn];
        uint16_t note = sfx->notes[c->note_id];
        int vraw = NOTE_VOL(note);

        if (vraw > 0) {
            key = NOTE_KEY(note);
            wave = NOTE_WAVE(note);
            is_music = c->is_music;
            vol = vraw * 4681;                        /* /7 in Q15 */
            inc = g_key_inc[key];
            /* Fractional position within the note, Q15. */
            int32_t t = (int32_t)((c->tick << 15) / c->note_len);
            /* Absolute (loop-wrapped) sample position, for vib/arp. */
            uint32_t pos = (uint32_t)c->note_id * c->note_len + c->tick;

            switch (NOTE_FX(note)) {
                case FX_SLIDE: {
                    /* Slides _from_ the previous note's pitch and volume;
                     * linear in Hz, so linear in phase increment. */
                    int32_t di = (int32_t)(inc - g_key_inc[c->prev_key]);
                    inc = g_key_inc[c->prev_key]
                        + (uint32_t)(int32_t)(((int64_t)di * t) >> 15);
                    if (c->prev_vol > 0)
                        vol = c->prev_vol + (int32_t)(((int64_t)(vol - c->prev_vol) * t) >> 15);
                    break;
                }
                case FX_VIBRATO: {
                    /* 7.5 Hz triangle, half a semitone: freq * (1 + 0.0595*v),
                     * v in [-0.25, 0.25] (zepto8 constants). */
                    uint32_t vt = pos % 2940u;        /* 22050 / 7.5 */
                    int32_t fr = (int32_t)((vt << 15) / 2940u);
                    int32_t v = fr - 16384;
                    if (v < 0) v = -v;
                    v -= 8192;                        /* Q15, +-0.25 */
                    inc += (uint32_t)(int32_t)(((int64_t)inc * ((v * 1949) >> 15)) >> 15);
                    break;
                }
                case FX_DROP:
                    inc -= (uint32_t)(int32_t)(((int64_t)inc * t) >> 15);
                    break;
                case FX_FADE_IN:
                    vol = (int32_t)(((int64_t)vol * t) >> 15);
                    break;
                case FX_FADE_OUT:
                    vol = (int32_t)(((int64_t)vol * (32768 - t)) >> 15);
                    break;
                case FX_ARP_FAST:
                case FX_ARP_SLOW: {
                    /* Iterate over the note's group of 4 at speed 4 (fast)
                     * or 8 (slow); halved when sfx speed <= 8. */
                    int fast = NOTE_FX(note) == FX_ARP_FAST;
                    int speed = sfx_speed(sfx);
                    uint32_t m = (uint32_t)((speed <= 8 ? 32 : 16) / (fast ? 4 : 8));
                    uint32_t n = pos / 2940u * m + pos % 2940u * m / 2940u;
                    uint16_t arp = sfx->notes[(c->note_id & ~3) | (n & 3)];
                    inc = g_key_inc[NOTE_KEY(arp)];
                    break;
                }
                default:
                    break;
            }
        }
    }

    /* Sequencer bookkeeping: advance the note position. */
    if (sn >= 0) {
        c->time++;
        if (++c->tick >= c->note_len) {
            c->tick = 0;
            if (c->note_id < 32) {
                uint16_t done = g_sfx[sn].notes[c->note_id];
                c->prev_key = NOTE_KEY(done);
                c->prev_vol = NOTE_VOL(done) * 4681;
            }
            c->note_id++;
            const snd_p8_sfx_t *sfx = &g_sfx[sn];
            int loop_range = (int)sfx->loop_end - (int)sfx->loop_start;
            if (loop_range > 0 && c->can_loop && c->note_id >= sfx->loop_end)
                c->note_id = sfx->loop_start
                           + (c->note_id - sfx->loop_start) % loop_range;
        }
        if (c->time >= c->end_smp)
            c->sfx = -1;
    }

    /* Declick (zepto8): on a harsh parameter jump, crossfade from the old
     * synth over ~7.7 ms while the new one continues. */
    uint32_t lo = inc < c->cur.inc ? inc : c->cur.inc;
    int32_t dv = vol - c->cur.vol;
    int32_t di = (int32_t)(inc - c->cur.inc);
    if (dv < 0) dv = -dv;
    if (di < 0) di = -di;
    if (dv > JUMP_VOL_Q15 || wave != c->cur.wave
        || (uint32_t)di > (uint32_t)(((uint64_t)lo * 655) >> 16)) {
        if (c->fade_q15 <= 0) c->fade = c->cur;
        c->fade_q15 = 32768;
    }
    if (key != c->cur.key) osc_set_key(&c->cur.osc, key);
    c->cur.inc = inc;
    c->cur.vol = vol;
    c->cur.key = (uint8_t)key;
    c->cur.wave = (uint8_t)wave;
    c->cur.is_music = (uint8_t)is_music;

    int32_t value = (vol > 0) ? psynth_sample(&c->cur, 1) : 0;

    if (c->fade_q15 > 0) {
        int32_t old = psynth_sample(&c->fade, 1);
        value = old + (int32_t)(((int64_t)(value - old) * (32768 - c->fade_q15)) >> 15);
        c->fade_q15 -= FADE_STEP_Q15;
    }
    return value;
}

/* ---- mixer hook (called from snd_core's render task) ---------------- */

/* Upsampler state: previous 22050 Hz mix sample. Chunks are even-sized, so
 * the 2x phase never straddles a call. */
static int32_t g_up_prev;

static int p8_active(void)
{
    if (g_music.pattern != -1) return 1;
    for (int i = 0; i < NUM_CHANNELS; i++)
        if (g_chan[i].sfx != -1 || g_chan[i].fade_q15 > 0) return 1;
    return 0;
}

int snd_p8_mix(int32_t *mix, int nframes)
{
    if (!p8_active()) {
        g_up_prev = 0;
        return 0;
    }
    for (int i = 0; i + 1 < nframes; i += 2) {
        update_music();
        int32_t s = 0;
        for (int ci = 0; ci < NUM_CHANNELS; ci++)
            s += chan_render(&g_chan[ci]);       /* each +-32767 */
        mix[i]     += (g_up_prev + s) >> 1;      /* interpolated midpoint */
        mix[i + 1] += s;
        g_up_prev = s;
    }
    return 1;
}

/* ---- control API (any task; short critical sections) ---------------- */

int snd_p8_load(const snd_p8_sfx_t *sfx, int nsfx,
                const snd_p8_pattern_t *patterns, int npat)
{
    if (nsfx < 0 || nsfx > 64 || npat < 0 || npat > 64) return -1;
    if ((nsfx > 0 && sfx == NULL) || (npat > 0 && patterns == NULL)) return -1;

    build_key_table();
    snd_p8_stop();
    /* Channels are idle now; the mixer only reads cart data via an active
     * channel, so the copy below cannot race a render. */
    memset(g_sfx, 0, sizeof(g_sfx));
    memset(g_pat, 0, sizeof(g_pat));
    if (nsfx > 0) memcpy(g_sfx, sfx, (size_t)nsfx * sizeof(*sfx));
    if (npat > 0) memcpy(g_pat, patterns, (size_t)npat * sizeof(*patterns));
    g_nsfx = nsfx;
    g_npat = npat;
    return 0;
}

void snd_p8_sfx(int n, int chan, int offset)
{
    if (n < -2 || n > 63 || chan < -1 || chan > 3 || offset > 31) return;

    P8_LOCK();
    if (n == -1) {              /* stop channel(s) not owned by music */
        for (int i = 0; i < NUM_CHANNELS; i++)
            if ((chan == -1 || chan == i) && !g_chan[i].is_music)
                g_chan[i].sfx = -1;
        P8_UNLOCK();
        return;
    }
    if (n == -2) {              /* stop looping */
        for (int i = 0; i < NUM_CHANNELS; i++)
            if ((chan == -1 || chan == i) && !g_chan[i].is_music
                && g_chan[i].can_loop) {
                g_chan[i].can_loop = 0;
                if (g_chan[i].sfx >= 0) chan_calc_end(&g_chan[i]);
            }
        P8_UNLOCK();
        return;
    }

    /* Auto channel: a free one, or one already playing this sfx; else steal
     * from music; else steal the fastest-speed channel (PICO-8 strategy). */
    if (chan == -1) {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (g_music.mask & (1 << i)) continue;
            if (g_chan[i].sfx == -1 || g_chan[i].sfx == n) {
                chan = i;
                break;
            }
        }
    }
    if (chan == -1) {
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (g_music.mask & (1 << i)) continue;
            if (g_chan[i].is_music) {
                chan = i;
                break;
            }
        }
    }
    if (chan == -1) {
        int fastest = 256;
        for (int i = 0; i < NUM_CHANNELS; i++) {
            if (g_music.mask & (1 << i)) continue;
            int idx = g_chan[i].sfx;
            if (idx < 0) continue;
            if (g_sfx[idx].speed <= fastest) {
                chan = i;
                fastest = g_sfx[idx].speed;
            }
        }
    }
    if (chan == -1) {
        P8_UNLOCK();
        return;
    }

    /* PICO-8 never plays the same sfx on two channels. */
    for (int i = 0; i < NUM_CHANNELS; i++)
        if (g_chan[i].sfx == n)
            g_chan[i].sfx = -1;

    /* If we steal from music, remember its sfx to resume later. */
    if (g_chan[chan].sfx != -1 && g_chan[chan].is_music)
        g_chan[chan].sfx_music = g_chan[chan].sfx;

    launch_sfx(n, chan,
               offset > 0 ? (uint32_t)offset
                            * NOTE_TICKS((uint32_t)sfx_speed(&g_sfx[n]))
                          : 0,
               0);
    P8_UNLOCK();
    snd_core_wake();
}

void snd_p8_music(int n, int fade_ms, int mask)
{
    if (n < -1 || n > 63) return;

    P8_LOCK();
    if (n == -1) {
        g_music.fade_step_q24 = fade_ms <= 0
            ? INT32_MIN / 2
            : -(int32_t)(((int64_t)g_music.fade_q24 * 1000)
                         / ((int64_t)fade_ms * P8_RATE));
        if (g_music.fade_step_q24 == 0 && g_music.fade_q24 > 0)
            g_music.fade_step_q24 = -1;
        P8_UNLOCK();
        return;
    }

    g_music.count = 0;
    g_music.mask = mask & 0xf;
    if (fade_ms > 0) {
        g_music.fade_q24 = 0;
        g_music.fade_step_q24 =
            (int32_t)(((int64_t)(1 << 24) * 1000)
                      / ((int64_t)fade_ms * P8_RATE));
        if (g_music.fade_step_q24 == 0) g_music.fade_step_q24 = 1;
    } else {
        g_music.fade_q24 = 1 << 24;
        g_music.fade_step_q24 = 0;
    }
    set_music_pattern(n);
    P8_UNLOCK();
    snd_core_wake();
}

void snd_p8_stop(void)
{
    P8_LOCK();
    set_music_pattern(-1);
    for (int i = 0; i < NUM_CHANNELS; i++) {
        g_chan[i].sfx = -1;
        g_chan[i].sfx_music = -1;
    }
    P8_UNLOCK();
    /* Channel declick fades ring out through the mixer on their own. */
}
