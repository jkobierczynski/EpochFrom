#pragma once

#include "GaiaCatalog.h"
#include <QVector>
#include <limits>

namespace epochfrom {

// Fits the capture epoch (calendar date) of an image by finding the epoch
// at which Gaia's proper-motion-propagated star positions best match a set
// of *observed* star positions from that image -- the core of this
// project's dating method. Direct C++ port of the validated Python
// prototype (fit_epoch.py): same 3-parameter model (epoch, RA zero-point
// offset, Dec zero-point offset), same tangent-plane residual formulation,
// same cross-match-then-fit structure.
class EpochFit {
public:
    struct ObservedStar {
        double raDeg = 0.0;
        double decDeg = 0.0;
    };

    struct Result {
        double epochJyear = 0.0;
        double epochSigmaYears = std::numeric_limits<double>::quiet_NaN();
        double raOffsetMas = 0.0;
        double decOffsetMas = 0.0;
        int nStarsUsed = 0;
        double medianMatchSepArcsec = std::numeric_limits<double>::quiet_NaN();
        double rmsResidualMas = std::numeric_limits<double>::quiet_NaN();
        bool converged = false;
        bool rankDeficient = false; // epoch and RA/Dec offset weren't fully
                                     // separable -- epochSigmaYears is
                                     // optimistic if this is set, same
                                     // warning the Python version prints.
    };

    // Cross-matches each observed star to its nearest Gaia catalog star
    // (within maxMatchArcsec, 1:1 deduplication -- closest separation
    // wins), then does a Levenberg-Marquardt fit for (epoch, ra_offset_mas,
    // dec_offset_mas). obsSigmaMas is the assumed per-star astrometric
    // precision, used both to weight residuals and to convert the fit's
    // covariance into an epoch uncertainty -- same role as the Python
    // version's obs_sigma_mas.
    static Result fit(const QVector<GaiaStar> &catalog,
                       const QVector<ObservedStar> &observed,
                       double obsSigmaMas = 300.0,
                       double t0GuessJyear = 2015.0,
                       double maxMatchArcsec = 3.0);

    // Synthetic validation, equivalent to fit_epoch.py --selftest: takes
    // real Gaia stars from `catalog`, propagates them to a made-up "true"
    // epoch, adds simulated Gaussian centroiding noise, and fits -- this
    // checks the whole pipeline's math (propagation + cross-match + fit)
    // against a known answer, using real field data rather than fabricated
    // stars. `seed` makes the noise reproducible for regression tests.
    static Result runSelfTest(const QVector<GaiaStar> &catalog,
                               double trueEpochJyear,
                               double noiseArcsec,
                               unsigned seed,
                               int nStars = 0 /* 0 = use all */);
};

} // namespace epochfrom
