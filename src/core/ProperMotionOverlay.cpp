#include "ProperMotionOverlay.h"
#include "SpaceMotion.h"
#include "Wcs.h"

#include <algorithm>
#include <cmath>

namespace epochfrom {

namespace {

struct Candidate {
    const GaiaStar *star = nullptr;
    double pixX = 0.0;
    double pixY = 0.0;
    double pmTotal = 0.0;
};

} // namespace

QVector<ProperMotionArrow> ProperMotionOverlay::compute(const QVector<GaiaStar> &stars, const Wcs &wcs,
                                                          const ProperMotionOverlayOptions &options,
                                                          QString *warning)
{
    QVector<ProperMotionArrow> result;

    if (!wcs.isValid()) {
        if (warning)
            *warning = QStringLiteral("WCS is not valid: %1").arg(wcs.errorMessage());
        return result;
    }

    // Propagate every catalog star to the display epoch and project it onto
    // this frame's pixel grid, discarding anything that lands outside the
    // actual image bounds (see the header comment on why this has to
    // happen before ranking, not after).
    QVector<Candidate> candidates;
    candidates.reserve(stars.size());
    for (const GaiaStar &star : stars) {
        const SpaceMotion::Position pos = SpaceMotion::propagate(star, options.targetEpochJyear);
        double px = 0.0, py = 0.0;
        if (!wcs.worldToPix(pos.raDeg, pos.decDeg, &px, &py))
            continue;
        if (options.imageWidth > 0 && options.imageHeight > 0) {
            if (px < 1.0 || px > options.imageWidth || py < 1.0 || py > options.imageHeight)
                continue;
        }
        Candidate c;
        c.star = &star;
        c.pixX = px;
        c.pixY = py;
        c.pmTotal = std::hypot(star.pmraMasYr, star.pmdecMasYr);
        candidates.push_back(c);
    }

    if (candidates.isEmpty()) {
        if (warning)
            *warning = QStringLiteral(
                "no Gaia stars with a proper-motion measurement fall inside this image's frame at "
                "the requested epoch -- check that the WCS/image really matches this Gaia catalog");
        return result;
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.pmTotal > b.pmTotal; });

    const int n = std::min<int>(std::max(options.topN, 0), candidates.size());
    if (n < options.topN && warning) {
        *warning = QStringLiteral("only %1 Gaia star(s) with proper motion fall inside this "
                                    "image's frame (requested top %2)")
                       .arg(n)
                       .arg(options.topN);
    }

    double pmSum = 0.0;
    for (int i = 0; i < n; ++i)
        pmSum += candidates[i].pmTotal;
    const double pmAverage = n > 0 ? pmSum / n : 0.0;

    // Guard against a degenerate all-zero-motion selection -- shouldn't
    // happen (GaiaCatalog::loadCsv already drops rows without pmra/pmdec),
    // but a hand-edited CSV could still zero every value out, and dividing
    // by that would blow every line up to infinity instead of just zero.
    const double lengthScale = (pmAverage > 1e-9 && options.averageArrowLengthPix > 0.0)
                                    ? options.averageArrowLengthPix / pmAverage
                                    : 0.0;

    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        const Candidate &c = candidates[i];

        // Direction of motion: propagate to a fixed, generous baseline on
        // either side of the display epoch and look at the pixel-space
        // vector between those two points, rather than differentiating at
        // the display epoch itself -- this stays numerically robust for a
        // star near the topN cutoff (slow, so a 1-year baseline would move
        // it by a fraction of a pixel) without needing a per-star adaptive
        // step size.
        const SpaceMotion::Position past =
            SpaceMotion::propagate(*c.star, options.targetEpochJyear - options.directionBaselineYears);
        const SpaceMotion::Position future =
            SpaceMotion::propagate(*c.star, options.targetEpochJyear + options.directionBaselineYears);
        double pastPixX = 0.0, pastPixY = 0.0, futurePixX = 0.0, futurePixY = 0.0;
        if (!wcs.worldToPix(past.raDeg, past.decDeg, &pastPixX, &pastPixY) ||
            !wcs.worldToPix(future.raDeg, future.decDeg, &futurePixX, &futurePixY)) {
            continue; // shouldn't happen this close to a point that just projected fine
        }

        double dx = futurePixX - pastPixX;
        double dy = futurePixY - pastPixY;
        const double norm = std::hypot(dx, dy);
        if (norm < 1e-9)
            continue; // no measurable direction -- shouldn't happen for pmTotal > 0
        dx /= norm;
        dy /= norm;

        ProperMotionArrow arrow;
        arrow.sourceId = c.star->sourceId;
        arrow.centerPixX = c.pixX;
        arrow.centerPixY = c.pixY;
        arrow.dirPixX = dx;
        arrow.dirPixY = dy;
        arrow.lengthPix = lengthScale * c.pmTotal;
        arrow.pmTotalMasYr = c.pmTotal;
        arrow.photGMeanMag = c.star->photGMeanMag;
        result.push_back(arrow);
    }

    return result;
}

} // namespace epochfrom
