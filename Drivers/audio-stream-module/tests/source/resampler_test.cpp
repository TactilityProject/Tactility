#include "doctest.h"

#include "../../source/resampler.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr double PI = 3.14159265358979323846;

std::vector<int16_t> sine(size_t frames, uint32_t rate, double frequency) {
    std::vector<int16_t> samples(frames);
    for (size_t i = 0; i < frames; i++) {
        samples[i] = static_cast<int16_t>(20000.0 * std::sin(2.0 * PI * frequency * static_cast<double>(i) / rate));
    }
    return samples;
}

/** The chunks are resampled one by one, like a player that writes small buffers */
std::vector<int16_t> resampleInChunks(const std::vector<int16_t>& input, size_t chunk_frames, uint32_t in_rate, uint32_t out_rate) {
    ContinuousResampler resampler;
    std::vector<int16_t> output;
    for (size_t start = 0; start < input.size(); start += chunk_frames) {
        const size_t frames = std::min(chunk_frames, input.size() - start);
        std::vector<int16_t> chunk(frames * out_rate / in_rate + 2);
        const size_t produced = resampler.process(input.data() + start, frames, 1, in_rate, out_rate, chunk.data(), chunk.size());
        output.insert(output.end(), chunk.begin(), chunk.begin() + produced);
    }
    return output;
}

}

TEST_CASE("resampler: the chunk size doesn't change the output") {
    const auto input = sine(4800, 48000, 440.0);
    const auto whole = resampleInChunks(input, input.size(), 48000, 44100);
    for (size_t chunk : { size_t(333), size_t(480), size_t(1024), size_t(7) }) {
        const auto chunked = resampleInChunks(input, chunk, 48000, 44100);
        // Only the end differs, where a chunk waits for the frame that follows it
        const size_t compared = std::min(whole.size(), chunked.size()) - 2;
        CHECK_GE(compared, whole.size() - 4);
        for (size_t i = 0; i < compared; i++) {
            // Differences in the last bit come from rounding of the position
            REQUIRE_LE(std::abs(chunked[i] - whole[i]), 2);
        }
    }
}

TEST_CASE("resampler: chunked output follows the input signal without jumps") {
    const auto input = sine(9600, 48000, 1000.0);
    const auto output = resampleInChunks(input, 481, 48000, 44100);
    REQUIRE_GT(output.size(), 8800u);
    // The error of linear interpolation of a 1 kHz sine is small. A phase restart per chunk would be far larger.
    for (size_t i = 0; i < output.size(); i++) {
        const double expected = 20000.0 * std::sin(2.0 * PI * 1000.0 * static_cast<double>(i) / 44100.0);
        REQUIRE_LT(std::abs(static_cast<double>(output[i]) - expected), 600.0);
    }
}

TEST_CASE("resampler: upsampling keeps the signal continuous") {
    const auto input = sine(1600, 16000, 200.0);
    const auto output = resampleInChunks(input, 160, 16000, 44100);
    REQUIRE_GT(output.size(), 4300u);
    for (size_t i = 0; i < output.size(); i++) {
        const double expected = 20000.0 * std::sin(2.0 * PI * 200.0 * static_cast<double>(i) / 44100.0);
        REQUIRE_LT(std::abs(static_cast<double>(output[i]) - expected), 400.0);
    }
}
