#include "EpochFit.h"
#include "AngleWrap.h"
#include "SpaceMotion.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <random>

namespace epochfrom {

namespace {

constexpr double kDegToRad = M_PI / 180.0;

struct MatchedPair {
    GaiaStar catalogStar;
    double obsRaDeg = 0.0;
    double obsDecDeg = 0.0;
    double sepArcsec = 0.0;
};

double angularSepArcsec(double ra1, double dec1, double ra2, double dec2)
{
    // Small-angle tangent-plane approximation is plenty accurate at the
    // few-arcsec match tolerances this is used for (matches the intent of
    // the Python prototype's astropy match_to_catalog_sky, without pulling
    // in a full haversine/Vincenty formula for a step that only needs to
    // be right to ~1%). wrapRaDiffDeg keeps that approximation honest near
    // RA 0/360, where two RA values for the same star can legitimately
    // land on opposite sides of the branch cut -- see AngleWrap.h.
    const double cosDec = std::cos(0.5 * (dec1 + dec2) * kDegToRad);
    const double dRa = wrapRaDiffDeg(ra1 - ra2) * cosDec;
    const double dDec = (dec1 - dec2);
    return std::hypot(dRa, dDec) * 3600.0;
}

// Nearest-neighbor cross-match, 1:1 deduplicated (closest separation wins
// on both sides) -- same contract as fit_epoch.py's cross_match().
QVector<MatchedPair> crossMatch(const QVector<GaiaStar> &catalog,
                                 const QVector<EpochFit::ObservedStar> &observed,
                                 double maxMatchArcsec)
{
    struct Candidate {
        int obsIdx;
        int catIdx;
        double sep;
    };
    QVector<Candidate> candidates;
    candidates.reserve(observed.size());

    for (int oi = 0; oi < observed.size(); ++oi) {
        int bestCat = -1;
        double bestSep = std::numeric_limits<double>::infinity();
        for (int ci = 0; ci < catalog.size(); ++ci) {
            const double sep = angularSepArcsec(observed[oi].raDeg, observed[oi].decDeg,
                                                 catalog[ci].raDeg, catalog[ci].decDeg);
            if (sep < bestSep) {
                bestSep = sep;
                bestCat = ci;
            }
        }
        if (bestCat >= 0 && bestSep < maxMatchArcsec)
            candidates.push_back({oi, bestCat, bestSep});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.sep < b.sep; });

    QVector<bool> obsUsed(observed.size(), false);
    QVector<bool> catUsed(catalog.size(), false);
    QVector<MatchedPair> out;
    out.reserve(candidates.size());
    for (const Candidate &c : candidates) {
        if (obsUsed[c.obsIdx] || catUsed[c.catIdx])
            continue;
        obsUsed[c.obsIdx] = true;
        catUsed[c.catIdx] = true;
        MatchedPair mp;
        mp.catalogStar = catalog[c.catIdx];
        mp.obsRaDeg = observed[c.obsIdx].raDeg;
        mp.obsDecDeg = observed[c.obsIdx].decDeg;
        mp.sepArcsec = c.sep;
        out.push_back(mp);
    }
    return out;
}

// Per-star tangent-plane residual in mas, for a trial (epoch, ra_offset_mas,
// dec_offset_mas) -- mirrors fit_epoch.py's residuals() closure exactly,
// including using the scalar separation (not signed x/y components) as the
// residual, which is what the validated Python version does.
Eigen::VectorXd residualsFor(const QVector<MatchedPair> &pairs, const Eigen::Vector3d &params,
                              double obsSigmaMas)
{
    const double t = params(0);
    const double dRaMas = params(1);
    const double dDecMas = params(2);

    Eigen::VectorXd r(pairs.size());
    for (int i = 0; i < pairs.size(); ++i) {
        const SpaceMotion::Position pred = SpaceMotion::propagate(pairs[i].catalogStar, t);
        const double cosDec = std::cos(pairs[i].catalogStar.decDeg * kDegToRad);
        // wrapRaDiffDeg -- see the comment on angularSepArcsec above and
        // AngleWrap.h: near RA 0/360 a plain subtraction can turn a
        // sub-arcsecond agreement into a ~360-degree-scale bogus residual,
        // which would otherwise dominate this fit's cost function outright
        // (there's no sigma-clipping in this LM loop the way
        // EquipmentCalibrator's fits have).
        const double dxMas = wrapRaDiffDeg(pairs[i].obsRaDeg - pred.raDeg) * cosDec * 3.6e6;
        const double dyMas = (pairs[i].obsDecDeg - pred.decDeg) * 3.6e6;
        const double rx = dxMas - dRaMas;
        const double ry = dyMas - dDecMas;
        r(i) = std::hypot(rx, ry) / obsSigmaMas;
    }
    return r;
}

Eigen::MatrixXd numericJacobian(const QVector<MatchedPair> &pairs, const Eigen::Vector3d &params,
                                  double obsSigmaMas)
{
    // Per-parameter absolute step: relative for the epoch (which sits at
    // ~O(1e3)), absolute for the two offset terms (which start at exactly
    // 0 -- a relative step there would be zero and the Jacobian column
    // would silently vanish, the same float64 pitfall the Python prototype
    // hit and fixed by working in tangent-plane mas throughout).
    const Eigen::Vector3d step(1e-4, 1e-3, 1e-3);
    const Eigen::VectorXd r0 = residualsFor(pairs, params, obsSigmaMas);
    Eigen::MatrixXd J(r0.size(), 3);
    for (int j = 0; j < 3; ++j) {
        Eigen::Vector3d p2 = params;
        p2(j) += step(j);
        const Eigen::VectorXd r1 = residualsFor(pairs, p2, obsSigmaMas);
        J.col(j) = (r1 - r0) / step(j);
    }
    return J;
}

// Small, self-contained Levenberg-Marquardt loop -- deliberately not
// pulling in Eigen's unsupported NonLinearOptimization module for a
// 3-parameter problem this well-behaved; mirrors what scipy.optimize.
// least_squares(method="lm") does for this same fit in the Python
// prototype closely enough to reproduce its results.
struct LmResult {
    Eigen::Vector3d params;
    Eigen::VectorXd residuals;
    Eigen::MatrixXd jacobian;
    bool converged = false;
};

LmResult levenbergMarquardt(const QVector<MatchedPair> &pairs, Eigen::Vector3d params,
                              double obsSigmaMas)
{
    double lambda = 1e-3;
    Eigen::VectorXd r = residualsFor(pairs, params, obsSigmaMas);
    double cost = 0.5 * r.squaredNorm();
    bool everImproved = false;
    bool converged = false;
    int consecutiveStalls = 0;

    // Per-parameter scale for judging "the step is now negligible" --
    // params mixes an epoch in years (O(1e3)) with two offsets in mas
    // (typically O(1-1e2) near the optimum). A single delta.norm() against
    // one absolute threshold is dominated by whichever parameter has the
    // larger natural units and effectively never fires for the others;
    // this scales each component by a sensible "close enough" step before
    // comparing, instead.
    const Eigen::Vector3d convergeScale(1e-6 /* yr */, 1e-4 /* mas */, 1e-4 /* mas */);

    // Cap how far lambda is allowed to escalate within one outer iteration.
    // Without a cap, repeated x10 damping can push it past ~1e15-1e20,
    // at which point the resulting step is so small that trialCost and
    // cost differ by less than double-precision rounding noise --
    // `trialCost < cost` then fails not because there's truly no improving
    // direction, but because floating point can no longer resolve the
    // (real, just tiny) improvement. That produced a real, machine-
    // dependent flaky failure: the same seed drew different noise on a
    // different std::normal_distribution implementation, landing the
    // search in a spot where this exact edge was hit on one machine and
    // not another, even though the resulting fit was equally good either
    // way. Capping lambda well short of that regime, and requiring several
    // consecutive fully-stalled outer iterations (not just one) before
    // giving up, makes "converged" a much more reliable signal.
    constexpr double kMaxLambda = 1e8;
    constexpr int kMaxConsecutiveStalls = 5;

    for (int iter = 0; iter < 300; ++iter) {
        const Eigen::MatrixXd J = numericJacobian(pairs, params, obsSigmaMas);
        const Eigen::Matrix3d JtJ = J.transpose() * J;
        const Eigen::Vector3d Jtr = J.transpose() * r;

        bool improved = false;
        while (!improved && lambda <= kMaxLambda) {
            Eigen::Matrix3d damped = JtJ;
            for (int k = 0; k < 3; ++k)
                damped(k, k) += lambda * std::max(JtJ(k, k), 1e-12);

            const Eigen::Vector3d delta = damped.ldlt().solve(-Jtr);
            const Eigen::Vector3d trialParams = params + delta;
            const Eigen::VectorXd trialR = residualsFor(pairs, trialParams, obsSigmaMas);
            const double trialCost = 0.5 * trialR.squaredNorm();

            if (std::isfinite(trialCost) && trialCost < cost) {
                const double costChange = cost - trialCost;
                params = trialParams;
                r = trialR;
                cost = trialCost;
                lambda = std::max(lambda / 10.0, 1e-12);
                improved = true;
                everImproved = true;
                consecutiveStalls = 0;
                const Eigen::Vector3d scaledDelta = delta.cwiseQuotient(convergeScale);
                if (costChange < 1e-9 * std::max(cost, 1e-12) || scaledDelta.norm() < 1.0)
                    converged = true;
            } else {
                lambda *= 10.0;
            }
        }

        if (!improved) {
            lambda = 1e-3; // give the next attempt a fresh start, not stuck at the cap
            ++consecutiveStalls;
            if (consecutiveStalls >= kMaxConsecutiveStalls) {
                converged = everImproved;
                break;
            }
            continue;
        }
        if (converged)
            break;
    }

    LmResult out;
    out.params = params;
    out.residuals = r;
    out.jacobian = numericJacobian(pairs, params, obsSigmaMas);
    out.converged = converged;
    return out;
}

EpochFit::Result fitMatched(const QVector<MatchedPair> &pairs, double obsSigmaMas,
                              double t0GuessJyear)
{
    EpochFit::Result out;
    if (pairs.isEmpty()) {
        out.converged = false;
        return out;
    }

    const LmResult lm = levenbergMarquardt(pairs, Eigen::Vector3d(t0GuessJyear, 0.0, 0.0),
                                             obsSigmaMas);

    out.epochJyear = lm.params(0);
    out.raOffsetMas = lm.params(1);
    out.decOffsetMas = lm.params(2);
    out.nStarsUsed = static_cast<int>(pairs.size());
    out.converged = lm.converged;

    double sumSep = 0.0;
    QVector<double> seps;
    seps.reserve(pairs.size());
    for (const MatchedPair &p : pairs)
        seps.push_back(p.sepArcsec);
    std::sort(seps.begin(), seps.end());
    out.medianMatchSepArcsec = seps.isEmpty() ? std::numeric_limits<double>::quiet_NaN()
                                               : seps[seps.size() / 2];
    Q_UNUSED(sumSep);

    const Eigen::VectorXd &r = lm.residuals; // dimensionless (mas / obsSigmaMas)
    out.rmsResidualMas = std::sqrt((r.array() * obsSigmaMas).square().mean());

    const int dof = std::max(static_cast<int>(r.size()) - 3, 1);
    const double residVar = r.squaredNorm() / dof;

    const Eigen::MatrixXd &J = lm.jacobian;
    const Eigen::Matrix3d JtJ = J.transpose() * J;
    Eigen::CompleteOrthogonalDecomposition<Eigen::Matrix3d> cod(JtJ);
    const Eigen::Matrix3d cov = cod.pseudoInverse() * residVar;
    const double var0 = cov(0, 0);
    out.epochSigmaYears = var0 > 0.0 ? std::sqrt(var0) : std::numeric_limits<double>::quiet_NaN();
    out.rankDeficient = cod.rank() < 3;

    return out;
}

} // namespace

EpochFit::Result EpochFit::fit(const QVector<GaiaStar> &catalog,
                                const QVector<ObservedStar> &observed, double obsSigmaMas,
                                double t0GuessJyear, double maxMatchArcsec)
{
    const QVector<MatchedPair> pairs = crossMatch(catalog, observed, maxMatchArcsec);
    return fitMatched(pairs, obsSigmaMas, t0GuessJyear);
}

EpochFit::Result EpochFit::runSelfTest(const QVector<GaiaStar> &catalog, double trueEpochJyear,
                                        double noiseArcsec, unsigned seed, int nStars)
{
    QVector<GaiaStar> subset = catalog;
    if (nStars > 0 && nStars < subset.size()) {
        std::mt19937 rng(seed);
        std::shuffle(subset.begin(), subset.end(), rng);
        subset.resize(nStars);
    }

    std::mt19937 rng(seed);
    const double noiseMas = noiseArcsec * 1000.0;
    std::normal_distribution<double> noise(0.0, noiseMas);

    QVector<ObservedStar> observed;
    observed.reserve(subset.size());
    for (const GaiaStar &star : subset) {
        const SpaceMotion::Position truth = SpaceMotion::propagate(star, trueEpochJyear);
        const double cosDec = std::cos(star.decDeg * kDegToRad);
        ObservedStar obs;
        obs.raDeg = truth.raDeg + (noise(rng) / 3.6e6) / std::max(cosDec, 1e-6);
        obs.decDeg = truth.decDeg + noise(rng) / 3.6e6;
        observed.push_back(obs);
    }

    // Same "start the fit a few years off truth" the Python self-test uses,
    // so recovery is a genuine test of the fit rather than starting at the
    // answer.
    return fit(subset, observed, noiseMas, trueEpochJyear - 3.0, 3.0);
}

} // namespace epochfrom
