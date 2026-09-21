/*
 * snd_port.h - Hardware port contract for the breezy_sound mixer.
 *
 * The engine (snd_core.c) is hardware-agnostic; each firmware project
 * provides one snd_port.c implementing the functions below for its audio
 * path (codec/BSP, bare I2S + amp, ...). Rules:
 *
 *   - snd_port_init() sets up the output but leaves it STOPPED (no clocks
 *     running / amp in standby). The engine starts it on the first note.
 *   - snd_port_start()/snd_port_stop() are called from the mixer task only,
 *     never concurrently, and strictly alternating after init.
 *   - snd_port_write() blocks until the DMA has room; that back-pressure is
 *     what paces the mixer task to the sample rate. Frames are int16 at
 *     44100 Hz: interleaved LR if snd_port_desc.stereo, mono otherwise.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <tactility/error.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool    stereo;      /* output frame format the port expects */
    int32_t limit_peak;  /* master limiter ceiling, tuned per speaker;
                          * what the board reproduces cleanly, not int16 max */
} snd_port_desc_t;

/* Provided by the project's snd_port.c. */
extern const snd_port_desc_t snd_port_desc;

error_t   snd_port_init(void);
void      snd_port_start(void);
void      snd_port_stop(void);
void      snd_port_write(const int16_t *frames, int nframes);

/* Tactility addition: releases the audio device reference taken by snd_port_init(), so the codec
 * is handed back when the app exits rather than held for the lifetime of the process. */
void      snd_port_deinit(void);

#ifdef __cplusplus
}
#endif
