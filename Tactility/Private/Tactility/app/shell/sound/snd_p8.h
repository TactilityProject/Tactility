/*
 * snd_p8.h - PICO-8 sfx/music tracker: the BreezyBox cart-sound API.
 *
 * A 4-channel PICO-8 player running inside the breezy_sound mixer task.
 * An app loads its cart sound data once with snd_p8_load(), then makes
 * fire-and-forget snd_p8_sfx()/snd_p8_music() calls -- no audio rendering
 * or per-frame pumping in the app. Sequencing and synthesis follow zepto8's
 * player (WTFPL), measured against PICO-8; the pork.p8 feature subset is
 * implemented (no custom instruments, SFX filters, or half-rate).
 *
 * Like snd_core.h, this header is the ABI that loadable ELF apps link
 * against by symbol name; it must stay identical across boards. It is
 * self-contained (no esp-idf includes) so apps can include it directly.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One sfx: speed, loop points, 32 packed notes
 * (bits: key 0-5, wave 6-8, volume 9-11, effect 12-14).
 * Matches the .p8 text encoding; see porklike's tools/p8sound2c.py. */
typedef struct {
    uint8_t  speed, loop_start, loop_end;
    uint16_t notes[32];
} snd_p8_sfx_t;

/* One music pattern: flags (1 = begin loop, 2 = end loop, 4 = stop) and one
 * byte per channel (bit 0x40 = channel off, low 6 bits = sfx index). */
typedef struct {
    uint8_t flags;
    uint8_t chan[4];
} snd_p8_pattern_t;

/* Load cart sound data (nsfx sfx, npat patterns, each up to 64). The data
 * is copied into the firmware, so the app's buffers may be temporary or
 * const. Stops any current playback. Returns 0, or -1 on bad arguments. */
int snd_p8_load(const snd_p8_sfx_t *sfx, int nsfx,
                const snd_p8_pattern_t *patterns, int npat);

/* PICO-8 sfx(): n = 0..63 to play, -1 stop channel(s), -2 stop looping.
 * chan -1 = auto-pick, offset = start note 0..31. */
void snd_p8_sfx(int n, int chan, int offset);

/* PICO-8 music(): n = 0..63 start pattern, -1 stop. fade_ms fades in (or
 * out, for n == -1); mask reserves channels from sfx stealing. */
void snd_p8_music(int n, int fade_ms, int mask);

/* Stop all tracker playback at once (sfx and music). Loaded data stays.
 * Also invoked by snd_all_off(), so app exit always silences the tracker. */
void snd_p8_stop(void);

#ifdef __cplusplus
}
#endif
