#include "StarDetector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace epochfrom {

namespace {

// Median via std::nth_element (O(n) average) rather than a full sort --
// matters here because sigmaClippedStats below calls this every clipping
// iteration, and a full-resolution frame is tens of millions of pixels.
// Reorders `values` as a side effect (nth_element's usual contract); callers
// pass a copy they don't need afterward.
double medianInPlace(QVector<float> &values)
{
    const int n = values.size();
    if (n == 0)
        return 0.0;
    const int mid = n / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double upper = double(values[mid]);
    if (n % 2 == 1)
        return upper;
    // std::max_element over the (still unsorted, but now correctly
    // partitioned by nth_element) lower half gives the other middle value
    // without a second full sort.
    const float lower = *std::max_element(values.begin(), values.begin() + mid);
    return (double(lower) + upper) / 2.0;
}

// Iteratively sigma-clipped median + std, matching
// astropy.stats.sigma_clipped_stats(data, sigma=3.0)'s defaults closely
// enough for this purpose (median center, population std, up to 5
// iterations, stopping early once a pass clips nothing new). Callers should
// pass a subsample rather than every pixel of a large image -- see
// StarDetector::detect -- since this is O(n) per iteration and runs up to
// maxIters times.
void sigmaClippedStats(const QVector<float> &pixels, double sigma, int maxIters, double *outMedian,
                        double *outStd)
{
    QVector<float> work = pixels;
    double median = 0.0, std = 0.0;
    auto computeStats = [&]() {
        QVector<float> forMedian = work;
        median = medianInPlace(forMedian);
        double sumSq = 0.0;
        for (float v : work) {
            const double d = double(v) - median;
            sumSq += d * d;
        }
        std = work.size() > 0 ? std::sqrt(sumSq / work.size()) : 0.0;
    };

    computeStats();
    for (int iter = 0; iter < maxIters; ++iter) {
        QVector<float> clipped;
        clipped.reserve(work.size());
        for (float v : work) {
            if (std::abs(double(v) - median) <= sigma * std)
                clipped.push_back(v);
        }
        if (clipped.size() == work.size() || clipped.isEmpty())
            break;
        work = clipped;
        computeStats(); // recompute against the newly-clipped set for the next pass (or as the final answer)
    }
    *outMedian = median;
    *outStd = std;
}

// Coarse tiled background estimate with bilinear interpolation between tile
// centers (the same idea as photutils' Background2D) -- a SINGLE
// frame-wide median is a poor background model for this project's actual
// subject matter: emission nebulae have broad, bright, smoothly-varying
// glow that a global median doesn't remove. Left in, that glow's own
// texture creates enormous numbers of spurious local maxima (real North
// America Nebula test frames: tens of thousands of "detections" against
// ~1000-1500 genuine matched stars) -- both wrong and, since every one of
// those gets a centroiding window pass, the reason detection was taking
// upwards of 20 seconds per sub. Removing the smooth large-scale trend
// first, before thresholding, fixes both.
QVector<float> estimateBackground(const QVector<float> &pixels, int width, int height, int tileSize,
                                    double *outGlobalStd)
{
    const int nTilesX = std::max(1, (width + tileSize - 1) / tileSize);
    const int nTilesY = std::max(1, (height + tileSize - 1) / tileSize);
    QVector<double> tileMedian(nTilesX * nTilesY, 0.0);
    QVector<double> tileStd(nTilesX * nTilesY, 0.0);

    for (int ty = 0; ty < nTilesY; ++ty) {
        for (int tx = 0; tx < nTilesX; ++tx) {
            const int x0 = tx * tileSize, x1 = std::min(width, x0 + tileSize);
            const int y0 = ty * tileSize, y1 = std::min(height, y0 + tileSize);
            QVector<float> sample;
            sample.reserve((x1 - x0) * (y1 - y0));
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                    sample.push_back(pixels[y * width + x]);
            double med = 0.0, sd = 0.0;
            sigmaClippedStats(sample, 3.0, 5, &med, &sd);
            tileMedian[ty * nTilesX + tx] = med;
            tileStd[ty * nTilesX + tx] = sd;
        }
    }

    // Robust global noise estimate: the MEDIAN of the per-tile stds, not
    // their mean or a whole-frame std -- a handful of tiles sitting on
    // bright nebula core (genuinely higher local scatter) shouldn't drag up
    // the noise estimate used for thresholding everywhere else.
    QVector<float> tileStdF(tileStd.size());
    for (int i = 0; i < tileStd.size(); ++i)
        tileStdF[i] = float(tileStd[i]);
    *outGlobalStd = medianInPlace(tileStdF);

    QVector<float> background(pixels.size());
    for (int y = 0; y < height; ++y) {
        const double fy = (y - tileSize / 2.0) / double(tileSize);
        int ty0 = int(std::floor(fy));
        const double wy = fy - ty0;
        int ty1 = ty0 + 1;
        ty0 = std::clamp(ty0, 0, nTilesY - 1);
        ty1 = std::clamp(ty1, 0, nTilesY - 1);
        for (int x = 0; x < width; ++x) {
            const double fx = (x - tileSize / 2.0) / double(tileSize);
            int tx0 = int(std::floor(fx));
            const double wx = fx - tx0;
            int tx1 = tx0 + 1;
            tx0 = std::clamp(tx0, 0, nTilesX - 1);
            tx1 = std::clamp(tx1, 0, nTilesX - 1);

            const double v00 = tileMedian[ty0 * nTilesX + tx0];
            const double v01 = tileMedian[ty0 * nTilesX + tx1];
            const double v10 = tileMedian[ty1 * nTilesX + tx0];
            const double v11 = tileMedian[ty1 * nTilesX + tx1];
            const double v0 = v00 * (1.0 - wx) + v01 * wx;
            const double v1 = v10 * (1.0 - wx) + v11 * wx;
            background[y * width + x] = float(v0 * (1.0 - wy) + v1 * wy);
        }
    }
    return background;
}

// A normalized 1D Gaussian kernel (sums to 1) with the given sigma, half
// width = ceil(3*sigma) samples each side.
QVector<double> gaussianKernel1D(double sigma, int *outHalfWidth)
{
    const int half = std::max(1, int(std::ceil(3.0 * sigma)));
    *outHalfWidth = half;
    QVector<double> kernel(2 * half + 1);
    double sum = 0.0;
    for (int i = -half; i <= half; ++i) {
        const double v = std::exp(-0.5 * (i * i) / (sigma * sigma));
        kernel[i + half] = v;
        sum += v;
    }
    for (double &v : kernel)
        v /= sum;
    return kernel;
}

// Separable 2D convolution (same kernel both axes), edge-clamped (nearest
// valid pixel repeated past the border) rather than zero-padded, so the
// image edges aren't artificially darkened -- irrelevant for stars well
// away from the border, and this tool doesn't need edge sources to be
// perfectly measured.
QVector<float> convolveSeparable(const QVector<float> &data, int width, int height,
                                   const QVector<double> &kernel, int halfWidth)
{
    QVector<float> tmp(data.size());
    // Horizontal pass.
    for (int y = 0; y < height; ++y) {
        const int rowBase = y * width;
        for (int x = 0; x < width; ++x) {
            double acc = 0.0;
            for (int k = -halfWidth; k <= halfWidth; ++k) {
                int sx = x + k;
                sx = std::clamp(sx, 0, width - 1);
                acc += kernel[k + halfWidth] * double(data[rowBase + sx]);
            }
            tmp[rowBase + x] = float(acc);
        }
    }
    // Vertical pass.
    QVector<float> out(data.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double acc = 0.0;
            for (int k = -halfWidth; k <= halfWidth; ++k) {
                int sy = y + k;
                sy = std::clamp(sy, 0, height - 1);
                acc += kernel[k + halfWidth] * double(tmp[sy * width + x]);
            }
            out[y * width + x] = float(acc);
        }
    }
    return out;
}

} // namespace

QVector<DetectedStar> StarDetector::detect(const FitsImageData &image,
                                             const StarDetectionOptions &options)
{
    QVector<DetectedStar> result;
    if (!image.loaded || image.width <= 0 || image.height <= 0)
        return result;

    const int width = image.width;
    const int height = image.height;

    // Tile size: large enough to average over plenty of noise, small enough
    // to track a nebula's brightness gradient reasonably -- a fixed
    // multiple of the expected stellar FWHM keeps it a detection-scale
    // decision rather than an image-size one. 64px covers a good range of
    // amateur setups (matches a several-hundred-mm-focal-length sub-pixel
    // scale reasonably, and is a lot larger than any real star).
    const int tileSize = std::max(32, int(std::round(16.0 * options.fwhmPx)));
    double std = 0.0;
    const QVector<float> background = estimateBackground(image.pixels, width, height, tileSize, &std);
    if (std <= 0.0)
        return result; // flat/empty image, nothing to detect

    QVector<float> bgSub(image.pixels.size());
    for (int i = 0; i < image.pixels.size(); ++i)
        bgSub[i] = image.pixels[i] - background[i];

    const double sigmaPx = options.fwhmPx / 2.3548200450309493; // FWHM = 2*sqrt(2*ln2)*sigma
    int halfWidth = 0;
    const QVector<double> kernel = gaussianKernel1D(sigmaPx, &halfWidth);
    double kernelSumSq = 0.0;
    for (double v : kernel)
        kernelSumSq += v * v;
    // 2D kernel is the outer product of two identical 1D kernels, so its
    // sum-of-squares (needed to propagate the background noise sigma
    // through the convolution) is (sum of 1D squares)^2.
    const double smoothedNoiseStd = std * std::sqrt(kernelSumSq * kernelSumSq);

    const QVector<float> smoothed = convolveSeparable(bgSub, width, height, kernel, halfWidth);
    const double threshold = options.thresholdSigma * smoothedNoiseStd;

    // Local-maximum peak finding on the smoothed image (3x3 neighborhood).
    // Border pixels (within halfWidth of any edge) are skipped -- both
    // because the centroiding window below needs room, and because
    // edge-of-field stars are usually vignetted/partial anyway.
    struct Peak {
        int x, y;
        float value;
    };
    QVector<Peak> peaks;
    const int margin = std::max(halfWidth, int(std::ceil(options.fwhmPx)));
    for (int y = margin; y < height - margin; ++y) {
        for (int x = margin; x < width - margin; ++x) {
            const float v = smoothed[y * width + x];
            if (v <= threshold)
                continue;
            bool isMax = true;
            for (int dy = -1; dy <= 1 && isMax; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0)
                        continue;
                    if (smoothed[(y + dy) * width + (x + dx)] > v) {
                        isMax = false;
                        break;
                    }
                }
            }
            if (!isMax)
                continue;

            // DAOFIND-style sharpness statistic (Stetson 1987): how much
            // the RAW central pixel stands out from its immediate
            // neighbors, relative to the peak's height in the
            // matched-filtered image. A true point source convolved with a
            // correctly-sized kernel lands in a fairly narrow band around
            // 1; broad/extended features (nebula texture, an out-of-focus
            // blob) score low since the raw center isn't much different
            // from its neighbors, while single-pixel spikes (hot pixels,
            // cosmic rays) score very high since the raw center vastly
            // exceeds both its neighbors and what the convolution -- which
            // spreads a delta function thin -- predicts. This is what
            // actually solved the "tens of thousands of spurious detections
            // on a bright nebula field" problem the tiled background
            // subtraction alone didn't fully fix.
            double neighborSum = 0.0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx != 0 || dy != 0)
                        neighborSum += bgSub[(y + dy) * width + (x + dx)];
            const double neighborMean = neighborSum / 8.0;
            const double sharpness = (double(bgSub[y * width + x]) - neighborMean) / v;
            if (sharpness < options.sharpLow || sharpness > options.sharpHigh)
                continue;

            peaks.push_back({x, y, v});
        }
    }

    // Centroid each peak against the ORIGINAL (unsmoothed) background
    // -subtracted data -- the smoothed image is only for finding candidate
    // locations robustly; a flux-weighted centroid on raw data is the more
    // accurate position estimate.
    const int centroidRadius = std::max(2, int(std::round(1.5 * options.fwhmPx)));
    QVector<DetectedStar> candidates;
    candidates.reserve(peaks.size());
    for (const Peak &peak : peaks) {
        double cx = peak.x;
        double cy = peak.y;
        double flux = 0.0;
        double varX = 0.0, varY = 0.0;
        for (int iter = 0; iter < options.centroidIterations; ++iter) {
            const int ix = int(std::round(cx));
            const int iy = int(std::round(cy));
            const int x0 = std::max(0, ix - centroidRadius);
            const int x1 = std::min(width - 1, ix + centroidRadius);
            const int y0 = std::max(0, iy - centroidRadius);
            const int y1 = std::min(height - 1, iy + centroidRadius);

            double sumW = 0.0, sumWx = 0.0, sumWy = 0.0;
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const double w = std::max(0.0f, bgSub[y * width + x]);
                    sumW += w;
                    sumWx += w * x;
                    sumWy += w * y;
                }
            }
            if (sumW <= 0.0)
                break;
            cx = sumWx / sumW;
            cy = sumWy / sumW;
            flux = sumW;

            if (iter == options.centroidIterations - 1) {
                double sumWx2 = 0.0, sumWy2 = 0.0;
                for (int y = y0; y <= y1; ++y) {
                    for (int x = x0; x <= x1; ++x) {
                        const double w = std::max(0.0f, bgSub[y * width + x]);
                        sumWx2 += w * (x - cx) * (x - cx);
                        sumWy2 += w * (y - cy) * (y - cy);
                    }
                }
                varX = sumWx2 / sumW;
                varY = sumWy2 / sumW;
            }
        }
        if (flux <= 0.0)
            continue;

        // Rough size estimate: FWHM = 2*sqrt(2*ln2)*sigma, sigma from the
        // window's marginal variance (average of the two axes).
        const double sigmaEst = std::sqrt(std::max(0.0, (varX + varY) / 2.0));
        const double fwhmEst = sigmaEst * 2.3548200450309493;
        if (fwhmEst < options.sizeRatioLow * options.fwhmPx ||
            fwhmEst > options.sizeRatioHigh * options.fwhmPx)
            continue;

        DetectedStar star;
        star.x = cx;
        star.y = cy;
        star.flux = flux;
        star.fwhmEstimatePx = fwhmEst;
        candidates.push_back(star);
    }

    // Merge near-duplicate detections (can happen when a saturated/plateaued
    // peak trips the 3x3 local-max test at more than one pixel): keep the
    // higher-flux detection of any pair closer than half the expected FWHM.
    // Bucketed by a mergeRadius-sized grid rather than compared pairwise --
    // an O(n^2) scan here is fine for a few hundred candidates but not for
    // a dense field with many thousands of real stars.
    std::sort(candidates.begin(), candidates.end(),
              [](const DetectedStar &a, const DetectedStar &b) { return a.flux > b.flux; });
    const double mergeRadius = 0.5 * options.fwhmPx;
    const double cellSize = std::max(1.0, mergeRadius);
    std::unordered_map<int64_t, QVector<int>> grid;
    auto cellKey = [](int cx, int cy) {
        return (int64_t(cx) << 32) ^ int64_t(uint32_t(cy));
    };
    for (int i = 0; i < candidates.size(); ++i) {
        const int cx = int(std::floor(candidates[i].x / cellSize));
        const int cy = int(std::floor(candidates[i].y / cellSize));
        bool tooClose = false;
        for (int ddy = -1; ddy <= 1 && !tooClose; ++ddy) {
            for (int ddx = -1; ddx <= 1 && !tooClose; ++ddx) {
                const auto it = grid.find(cellKey(cx + ddx, cy + ddy));
                if (it == grid.end())
                    continue;
                for (int j : it->second) {
                    const double dx = candidates[i].x - candidates[j].x;
                    const double dy = candidates[i].y - candidates[j].y;
                    if (std::hypot(dx, dy) < mergeRadius) {
                        tooClose = true;
                        break;
                    }
                }
            }
        }
        if (tooClose)
            continue;
        result.push_back(candidates[i]);
        grid[cellKey(cx, cy)].push_back(i);
    }

    return result;
}

} // namespace epochfrom
