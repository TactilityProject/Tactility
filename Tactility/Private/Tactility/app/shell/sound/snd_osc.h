/*
 * snd_osc.h - Shared fixed-point oscillator core (private to breezy_sound).
 *
 * One waveform generator used by both front ends: the snd_note_* voice bank
 * (snd_core.c, 44100 Hz) and the PICO-8 tracker (snd_p8.c, 22050 Hz).
 *
 * Waveform shapes, duty cycles and relative amplitudes follow zepto8's
 * synth.cpp (WTFPL), whose constants were measured against PICO-8 WAV
 * exports. Samples are the zepto8 formula value scaled by 32768, so the
 * loudest waves (triangle, tilted saw, phaser, epiano) peak at +/-16384 and
 * e.g. square at +/-8192. All state is integer; the only floats allowed are
 * one-off per-note setup in the callers.
 */
#pragma once

#include <stdint.h>

/* Per-sample noise filter drive: scale = freq / freq(key 63). Derived from a
 * phase increment as (inc * NOISE_C) >> 32 where NOISE_C = rate * 32768 /
 * 2489.0159 for the caller's sample rate. */
#define SND_OSC_NOISE_C_44100 580580u
#define SND_OSC_NOISE_C_22050 290290u

typedef struct {
    uint32_t phase;    /* Q32 cycles */
    uint32_t phase2;   /* detuned partner (phaser) */
    uint32_t rng;      /* xorshift state, never 0 */
    int32_t  nsmp;     /* brown-noise one-pole state, ~Q14 */
    int32_t  nmul;     /* noise key-tracking gain 1.5*(1+k^2), Q13 */
} osc_t;

static inline void osc_reset(osc_t *o, uint32_t seed)
{
    o->phase = 0;
    o->phase2 = 0;
    o->nsmp = 0;
    o->nmul = 12288;   /* key 63: plain 1.5x */
    if (o->rng == 0) o->rng = seed ? seed : 0x12345678;
}

/* Key-tracked noise gain: k = 1 - key/63, gain = 1.5 * (1 + k^2). */
static inline void osc_set_key(osc_t *o, int key)
{
    if (key < 0) key = 0;
    if (key > 63) key = 63;
    int32_t kq = (int32_t)(63 - key) * 32768 / 63;              /* Q15 */
    o->nmul = 12288 + (int32_t)((((int64_t)kq * kq >> 15) * 12288) >> 15);
}

/* Triangle from a 15-bit phase position, range +/-16384. */
static inline int32_t osc_tri15(uint32_t pos)
{
    return (pos < 16384) ? (int32_t)(pos * 2) - 16384
                         : 49150 - (int32_t)(pos * 2);
}

/* Cheap pseudo-sine, range +/-16384: parabolic shaping of the triangle. */
static inline int32_t osc_sin15(uint32_t phase)
{
    int32_t t = osc_tri15((phase >> 17) & 0x7fff);
    int32_t a = t < 0 ? -t : t;
    return (t * (32768 - a)) >> 14;   /* peak 2^28, fits int32 */
}

/*
 * One sample of the given wave. `inc` is the phase increment this sample
 * will advance by (needed by the noise filter), `noise_c` one of the
 * SND_OSC_NOISE_C_* constants. Does not advance the phase; call osc_advance
 * afterwards (skipped entirely for silent notes, matching zepto8).
 */
static inline int32_t osc_sample(osc_t *o, int wave, uint32_t inc,
                                 uint32_t noise_c)
{
    uint32_t pos = o->phase >> 17;   /* 0..32767, t in Q15 */
    switch (wave) {
        case 0:  /* triangle: (1 - |4t-2|) * 0.5 */
            return osc_tri15(pos);
        case 1:  /* tilted saw, break at t = 0.875, amplitude 0.5 */
            return (pos < 28672) ? (int32_t)(pos * 8 / 7) - 16384
                                 : (int32_t)((32767 - pos) * 8) - 16384;
        case 2:  /* saw: 0.653 * (t - 0.5-ish) */
            return (((int32_t)pos - 16384) * 21398) >> 15;
        case 3:  /* square, +/-0.25 */
            return (pos < 16384) ? 8192 : -8192;
        case 4:  /* pulse, duty 0.316, +/-0.25 */
            return (pos < 10355) ? 8192 : -8192;
        case 5: {/* organ: (t<.5 ? 3-|24t-6| : 1-|16t-12|) / 9 */
            int32_t v;
            if (pos < 16384) {
                v = 24 * (int32_t)pos - 196608;
                v = 98304 - (v < 0 ? -v : v);
            } else {
                v = 16 * (int32_t)pos - 393216;
                v = 32768 - (v < 0 ? -v : v);
            }
            return v / 9;
        }
        case 6: {/* key-tracked brown noise (one-pole, cutoff follows freq) */
            int32_t scale = (int32_t)(((uint64_t)inc * noise_c) >> 32);
            if (o->rng == 0) o->rng = 0x12345678;   /* xorshift can't run from 0 */
            o->rng ^= o->rng << 13;
            o->rng ^= o->rng >> 17;
            o->rng ^= o->rng << 5;
            int32_t rnd = (int32_t)(o->rng & 0x7fff) - 16384;
            int64_t num = (int64_t)o->nsmp * 32768 + (int64_t)scale * rnd;
            o->nsmp = (int32_t)(num / (32768 + scale));
            /* nsmp is Q14 (+/-16384 = 1.0); >>12 maps 1.0 * nmul to the
             * formula-times-32768 output domain of the other waves. */
            int32_t s = (int32_t)(((int64_t)o->nsmp * o->nmul) >> 12);
            if (s > 32767) s = 32767;
            else if (s < -32767) s = -32767;
            return s;
        }
        case 7: {/* phaser: (2*tri(t) + tri(t * 109/110)) / 6 */
            return (2 * osc_tri15(pos) + osc_tri15(o->phase2 >> 17)) / 3;
        }
        case 8:  /* epiano: sine + soft 4x "tine" overtone (extension) */
            return (3 * osc_sin15(o->phase) + osc_sin15(o->phase * 4)) / 4;
        default:
            return 0;
    }
}

static inline void osc_advance(osc_t *o, uint32_t inc)
{
    o->phase  += inc;
    o->phase2 += inc - inc / 110;   /* zepto8's 109:110 phaser detune */
}

/* Cross-module glue (both .c files live in this component). */
int  snd_p8_mix(int32_t *mix, int nframes);  /* add tracker audio at 44100 */
void snd_core_wake(void);                    /* kick the mixer task awake */
