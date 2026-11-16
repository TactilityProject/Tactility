// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

/**
 * Linear-interpolation resampler for a stream that arrives in chunks of any size. It continues where the previous
 * chunk stopped, so the chunk boundaries don't cause discontinuities, which would be audible as crackling.
 * Operates on interleaved 16-bit PCM.
 */
struct ContinuousResampler {
    static constexpr uint8_t MAX_CHANNELS = 8;

    /** Position of the next output frame in input frames, relative to the start of the next chunk. -1 is the last frame of the previous chunk. */
    double position = 0.0;
    /** The last frame of the previous chunk. The state has a fixed size, so it can be copied to undo a chunk. */
    int16_t previous[MAX_CHANNELS] = {};
    uint8_t previousChannels = 0;

    /**
     * Consumes all of a chunk of input frames, for a stream with a fixed channel count and rates.
     * @param[out] out room for out_frame_capacity frames. in_frames * out_rate / in_rate + 2 is enough.
     * @return the number of output frames, 0 when there are more than MAX_CHANNELS channels
     */
    size_t process(const int16_t* in, size_t in_frames, uint8_t channels, uint32_t in_rate, uint32_t out_rate, int16_t* out, size_t out_frame_capacity) {
        if (in_frames == 0 || channels > MAX_CHANNELS) {
            return 0;
        }
        if (previousChannels != channels) {
            for (auto& sample : previous) {
                sample = 0;
            }
            previousChannels = channels;
            position = 0.0;
        }

        const double ratio = static_cast<double>(in_rate) / static_cast<double>(out_rate);
        size_t out_frames = 0;
        while (out_frames < out_frame_capacity) {
            const double base = std::floor(position);
            const auto index = static_cast<int64_t>(base);
            // The next output frame needs the following chunk's first frame
            if (index + 1 >= static_cast<int64_t>(in_frames)) {
                break;
            }
            const double fraction = position - base;
            for (uint8_t channel = 0; channel < channels; channel++) {
                const double a = (index < 0) ? previous[channel] : in[static_cast<size_t>(index) * channels + channel];
                const double b = in[static_cast<size_t>(index + 1) * channels + channel];
                out[out_frames * channels + channel] = static_cast<int16_t>(a + (b - a) * fraction);
            }
            out_frames++;
            position += ratio;
        }

        position -= static_cast<double>(in_frames);
        for (uint8_t channel = 0; channel < channels; channel++) {
            previous[channel] = in[(in_frames - 1) * channels + channel];
        }
        return out_frames;
    }
};
