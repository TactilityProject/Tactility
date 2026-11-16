#pragma once

#include <tactility/device.h>

/**
 * The speaker amplifier is only on while the codec plays sound and no headphones are plugged in.
 * Left on, it amplifies the codec's analog start-up and shut-down noise as a loud pop.
 */

/** Switches the amplifier off. Call when io_expander0 has started. */
void tab5_speaker_amp_init();

/** Follows the codec output of the ES8388 device, which has to have started */
void tab5_speaker_amp_follow_codec(Device* codec);

/** @return false when the amplifier couldn't be updated, so the caller can retry */
bool tab5_speaker_amp_set_headphones(bool plugged_in);
