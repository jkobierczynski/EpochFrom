#include "ImageStretch.h"

#include <algorithm>
#include <cmath>

namespace epochfrom {

QVector<unsigned char> ImageStretch::autoStretch(const FitsImageData &image, const Options &options)
{
    QVector<unsigned char> out;
    if (!image.loaded || image.pixels.isEmpty() || image.width <= 0 || image.height <= 0)
        return out;

    const qsizetype n = image.pixels.size();

    // Percentile bounds via nth_element on a scratch copy -- O(n) average,
    // versus O(n log n) for a full sort, which matters here: a single
    // full-resolution sub can be tens of millions of pixels.
    QVector<float> scratch = image.pixels;
    auto percentileIndex = [n](double percentile) {
        const double p = std::clamp(percentile, 0.0, 100.0) / 100.0;
        return static_cast<qsizetype>(std::clamp<qint64>(
            static_cast<qint64>(p * static_cast<double>(n - 1)), 0, n - 1));
    };
    const qsizetype lowIdx = percentileIndex(options.lowPercentile);
    const qsizetype highIdx = percentileIndex(options.highPercentile);

    std::nth_element(scratch.begin(), scratch.begin() + lowIdx, scratch.end());
    const float lowVal = scratch[lowIdx];
    // The low pass already partitions [0, lowIdx] <= lowVal; refine just
    // the remaining range for the high percentile instead of the whole
    // buffer again.
    std::nth_element(scratch.begin() + lowIdx, scratch.begin() + highIdx, scratch.end());
    const float highVal = scratch[highIdx];

    const double range = static_cast<double>(highVal) - static_cast<double>(lowVal);
    const double asinhA = std::max(options.asinhAmount, 1e-6);
    const double asinhNorm = std::asinh(asinhA);

    out.resize(n);
    if (range <= 1e-12) {
        // Degenerate (flat) image -- every pixel maps to the same gray
        // level rather than dividing by ~zero.
        std::fill(out.begin(), out.end(), static_cast<unsigned char>(128));
        return out;
    }

    for (qsizetype i = 0; i < n; ++i) {
        const double t = std::clamp((static_cast<double>(image.pixels[i]) - lowVal) / range, 0.0, 1.0);
        const double stretched = std::asinh(t * asinhA) / asinhNorm;
        out[i] = static_cast<unsigned char>(std::lround(std::clamp(stretched, 0.0, 1.0) * 255.0));
    }

    return out;
}

} // namespace epochfrom
