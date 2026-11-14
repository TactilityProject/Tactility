#include <Tactility/app/touchcalibration/CalibrationMath.h>

#include <cmath>

namespace tt::app::touchcalibration {

Point toNativePoint(Point point, Rotation rotation, int32_t nativeWidth, int32_t nativeHeight) {
    switch (rotation) {
        case Rotation::Rotate90:
            return { .x = point.y, .y = nativeHeight - 1 - point.x };
        case Rotation::Rotate180:
            return { .x = nativeWidth - 1 - point.x, .y = nativeHeight - 1 - point.y };
        case Rotation::Rotate270:
            return { .x = nativeWidth - 1 - point.y, .y = point.x };
        default:
            return point;
    }
}

bool computeRange(const int32_t* targets, const int32_t* samples, size_t count, int32_t nativeMax, int32_t& min, int32_t& max) {
    if (count < 2) {
        return false;
    }

    double targetMean = 0.0;
    double sampleMean = 0.0;
    for (size_t i = 0; i < count; i++) {
        targetMean += targets[i];
        sampleMean += samples[i];
    }
    targetMean /= static_cast<double>(count);
    sampleMean /= static_cast<double>(count);

    // Least squares fit of sample = offset + scale * target
    double covariance = 0.0;
    double variance = 0.0;
    for (size_t i = 0; i < count; i++) {
        covariance += (targets[i] - targetMean) * (samples[i] - sampleMean);
        variance += (targets[i] - targetMean) * (targets[i] - targetMean);
    }
    if (variance <= 0.0) {
        return false;
    }

    const double scale = covariance / variance;
    if (scale <= 0.0) {
        return false;
    }
    const double offset = sampleMean - scale * targetMean;
    min = static_cast<int32_t>(std::lround(offset));
    max = static_cast<int32_t>(std::lround(offset + scale * nativeMax));
    return true;
}

} // namespace tt::app::touchcalibration
