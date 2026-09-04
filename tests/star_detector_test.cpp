// Unit test for StarDetector: builds a synthetic image (Gaussian PSF stars
// at known sub-pixel positions, on a noisy background) entirely in memory
// -- no FITS file needed, since FitsImageData is just pixel data + a couple
// of header fields -- and checks that detection recovers each star's
// position to well within a pixel, with no massive spurious-detection
// count from the noise alone.

#include "StarDetector.h"

#include <QCoreApplication>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <random>

using namespace epochfrom;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const int width = 200;
    const int height = 150;
    const double backgroundLevel = 1000.0;
    const double noiseSigma = 15.0;
    const double fwhmPx = 4.0;
    const double sigmaPx = fwhmPx / 2.3548200450309493;

    struct TrueStar {
        double x, y, amplitude;
    };
    // A range of brightnesses (some well above threshold, one modest) at
    // sub-pixel positions away from integer coordinates, spaced far enough
    // apart that PSF wings don't meaningfully overlap.
    const QVector<TrueStar> trueStars = {
        {40.3, 30.7, 4000.0}, {120.6, 60.2, 8000.0}, {70.1, 110.4, 1500.0},
        {160.8, 100.5, 6000.0}, {25.5, 90.9, 2500.0},
    };

    FitsImageData image;
    image.loaded = true;
    image.width = width;
    image.height = height;
    image.pixels.resize(width * height);

    std::mt19937 rng(12345);
    std::normal_distribution<double> noise(0.0, noiseSigma);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double v = backgroundLevel + noise(rng);
            for (const TrueStar &s : trueStars) {
                const double dx = x - s.x;
                const double dy = y - s.y;
                v += s.amplitude * std::exp(-0.5 * (dx * dx + dy * dy) / (sigmaPx * sigmaPx));
            }
            image.pixels[y * width + x] = float(v);
        }
    }

    StarDetectionOptions options;
    options.fwhmPx = fwhmPx;
    options.thresholdSigma = 6.0;
    const QVector<DetectedStar> detected = StarDetector::detect(image, options);

    out << "Placed " << trueStars.size() << " synthetic stars, detected " << detected.size() << "\n";

    bool ok = true;
    if (detected.size() < trueStars.size()) {
        out << "FAIL: fewer detections than placed stars -- missed a real star\n";
        ok = false;
    }
    // A handful of spurious noise-only detections wouldn't be surprising in
    // general, but with this SNR/threshold and a fixed seed there should be
    // none; a large excess would signal a broken threshold/local-max
    // implementation (e.g. detecting on every pixel).
    if (detected.size() > trueStars.size() + 3) {
        out << "FAIL: far more detections than placed stars -- likely spurious noise detections\n";
        ok = false;
    }

    const double posTolPx = 0.15; // generous for this SNR, tight enough to catch a real centroid bug
    for (const TrueStar &s : trueStars) {
        double bestDist = std::numeric_limits<double>::infinity();
        const DetectedStar *bestMatch = nullptr;
        for (const DetectedStar &d : detected) {
            const double dist = std::hypot(d.x - s.x, d.y - s.y);
            if (dist < bestDist) {
                bestDist = dist;
                bestMatch = &d;
            }
        }
        out << QString("  true (%1, %2) amp=%3 -> nearest detection %4 px away")
                   .arg(s.x, 0, 'f', 2)
                   .arg(s.y, 0, 'f', 2)
                   .arg(s.amplitude, 0, 'f', 0)
                   .arg(bestDist, 0, 'f', 3);
        if (bestMatch)
            out << QString(" at (%1, %2), flux=%3")
                       .arg(bestMatch->x, 0, 'f', 2)
                       .arg(bestMatch->y, 0, 'f', 2)
                       .arg(bestMatch->flux, 0, 'f', 0);
        out << "\n";
        if (!(bestDist < posTolPx)) {
            out << "  FAIL: no detection within tolerance of this star\n";
            ok = false;
        }
    }

    // Brighter stars should generally end up with higher reported flux --
    // a coarse sanity check on the flux measurement, not a precise one.
    if (detected.size() >= 2) {
        QVector<DetectedStar> sorted = detected;
        std::sort(sorted.begin(), sorted.end(),
                  [](const DetectedStar &a, const DetectedStar &b) { return a.flux > b.flux; });
        if (!(sorted.first().flux > sorted.last().flux)) {
            out << "FAIL: flux values don't discriminate between detections at all\n";
            ok = false;
        }
    }

    // An empty/flat image (no stars, only uniform background, zero noise)
    // should detect nothing rather than crash or divide by zero (std=0 in
    // the background stats).
    FitsImageData flat;
    flat.loaded = true;
    flat.width = 50;
    flat.height = 50;
    flat.pixels.fill(1000.0f, 50 * 50);
    const QVector<DetectedStar> onFlat = StarDetector::detect(flat, options);
    if (!onFlat.isEmpty()) {
        out << "FAIL: detected spurious stars on a perfectly flat image\n";
        ok = false;
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
