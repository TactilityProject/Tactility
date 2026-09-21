/*
 * snd_core.h - Minimal voice mixer: the BreezyBox sound API for ELF apps.
 *
 * 8 voices, PICO-8-style waveforms, simple attack/release envelopes, plus one
 * raw PCM stream, rendered by a background task into the board's audio output
 * (see snd_port.h for the per-board hardware glue). Callers just post events:
 * nothing here blocks on audio.
 *
 * This header is the ABI that loadable ELF apps link against by symbol name;
 * it must stay identical across boards.
 */
#pragma once

#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SND_NUM_VOICES 8

/* Waveforms 0-7 mirror the PICO-8 numbering; 8+ are extensions. */
enum {
    SND_WAVE_TRIANGLE = 0,
    SND_WAVE_TILTED_SAW,
    SND_WAVE_SAW,
    SND_WAVE_SQUARE,
    SND_WAVE_PULSE,
    SND_WAVE_ORGAN,
    SND_WAVE_NOISE,
    SND_WAVE_PHASER,
    SND_WAVE_EPIANO,       /* sine + soft tine overtone (Rhodes-ish) */
    SND_WAVE_COUNT,
};

/* Start the mixer task (idempotent). Call after the board's audio hardware
 * prerequisites are up (see the project's snd_port.c). */
error_t snd_init(void);

/* Start a note. voice -1 = auto-allocate (steals the oldest if full).
 * wave 0-7, vol 0-255. Returns the voice index used, or -1. */
int snd_note_on(int voice, float freq_hz, int wave, int vol);

/* Release a note (envelope fades it out). */
void snd_note_off(int voice);

/* Release everything: all voices, an open PCM stream, and any PICO-8
 * tracker playback (snd_p8.h). Call when an app/command exits. */
void snd_all_off(void);

/* Master volume, 0..100 percent, applied to the final combined bus
 * (synth voices + PCM stream). Defaults to 100 at startup. */
void snd_set_volume(int pct);
int  snd_get_volume(void);

/*
 * PCM stream: one raw int16 stream mixed in as a 9th source alongside the
 * synth voices. Apps render at normal int16 full scale; the mixer owns the
 * mapping to each board's output. Single stream, single owner. The app
 * renders at its own pace, polling snd_stream_space() from its main loop;
 * underrun plays silence, never stalls. Master volume applies. On boards
 * with mono output, stereo input is downmixed to (L+R)/2.
 */

/* Claim the stream. rate must be 44100 (the mixer rate; no resampler),
 * channels 1 or 2 (interleaved LR). Returns 0, or -1 if unsupported or
 * already open. */
int snd_stream_open(int rate, int channels);

/* Frames writable right now without dropping (in the open channel count). */
int snd_stream_space(void);

/* Queue up to nframes interleaved frames; returns frames accepted. */
int snd_stream_write(const int16_t *frames, int nframes);

/* Release the stream; synth-only mixing resumes. */
void snd_stream_close(void);

#ifdef __cplusplus
}
#endif
