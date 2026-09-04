#include "EquipmentCalibrator.h"
#include "FitsImage.h"
#include "LinearWcs.h"
#include "PlateSolver.h"
#include "SipPolynomial.h"
#include "SpaceMotion.h"

#include <Eigen/Dense>

#include <QHash>
#include <QMap>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <random>

namespace epochfrom {

namespace {

double angularSepArcsec(double ra1Deg, double dec1Deg, double ra2Deg, double dec2Deg)
{
    // Haversine great-circle separation -- exact at any distance, not just
    // a small-angle approximation, at negligible extra cost given the
    // modest star counts involved here.
    const double ra1 = ra1Deg * M_PI / 180.0, dec1 = dec1Deg * M_PI / 180.0;
    const double ra2 = ra2Deg * M_PI / 180.0, dec2 = dec2Deg * M_PI / 180.0;
    const double sinDLat = std::sin((dec2 - dec1) / 2.0);
    const double sinDLon = std::sin((ra2 - ra1) / 2.0);
    const double a = sinDLat * sinDLat + std::cos(dec1) * std::cos(dec2) * sinDLon * sinDLon;
    const double c = 2.0 * std::asin(std::min(1.0, std::sqrt(a)));
    return c * 180.0 / M_PI * 3600.0;
}

struct Match {
    int detIdx;
    int gaiaIdx;
    double sepArcsec;
};

// Nearest-neighbor cross-match with 1:1 deduplication (closest separation
// wins for any detection or catalog star claimed more than once) -- same
// structure as the Python prototype's pandas dedup chain
// (.sort_values("sep").drop_duplicates("gaia_idx").drop_duplicates("det_idx")).
QVector<Match> crossMatch(const QVector<double> &detRaDeg, const QVector<double> &detDecDeg,
                           const QVector<double> &gaiaRaDeg, const QVector<double> &gaiaDecDeg,
                           double maxSepArcsec)
{
    // A brute-force n(detections) x n(catalog) scan is fine for the small
    // synthetic tests, but real calibration runs pool a few hundred to a
    // couple thousand detections per sub against a multi-thousand-star deep
    // catalog across up to a handful of subs -- squarely too slow at that
    // scale (tens of seconds per sub, minutes overall). Since the match
    // tolerance is a few arcsec and the field is degrees wide, sorting the
    // catalog by declination and binary-searching a +/-tolerance window
    // around each detection's Dec (only Dec, not a full 2D index -- RA
    // narrows almost nothing extra at this tolerance, and Dec alone cuts
    // the search from thousands of candidates to a handful) turns this from
    // O(n*m) into effectively O(n log m).
    const int nGaia = gaiaRaDeg.size();
    QVector<int> order(nGaia);
    for (int i = 0; i < nGaia; ++i)
        order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return gaiaDecDeg[a] < gaiaDecDeg[b]; });
    QVector<double> sortedDec(nGaia);
    for (int i = 0; i < nGaia; ++i)
        sortedDec[i] = gaiaDecDeg[order[i]];

    const double tolDeg = maxSepArcsec / 3600.0;

    QVector<Match> candidates;
    candidates.reserve(detRaDeg.size());
    for (int di = 0; di < detRaDeg.size(); ++di) {
        const auto loIt = std::lower_bound(sortedDec.begin(), sortedDec.end(), detDecDeg[di] - tolDeg);
        const auto hiIt = std::upper_bound(sortedDec.begin(), sortedDec.end(), detDecDeg[di] + tolDeg);
        int bestGi = -1;
        double bestSep = std::numeric_limits<double>::infinity();
        for (auto it = loIt; it != hiIt; ++it) {
            const int gi = order[it - sortedDec.begin()];
            const double sep = angularSepArcsec(detRaDeg[di], detDecDeg[di], gaiaRaDeg[gi], gaiaDecDeg[gi]);
            if (sep < bestSep) {
                bestSep = sep;
                bestGi = gi;
            }
        }
        if (bestGi >= 0 && bestSep < maxSepArcsec)
            candidates.push_back({di, bestGi, bestSep});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Match &a, const Match &b) { return a.sepArcsec < b.sepArcsec; });

    QVector<Match> result;
    result.reserve(candidates.size());
    QVector<bool> detUsed(detRaDeg.size(), false);
    QVector<bool> gaiaUsed(gaiaRaDeg.size(), false);
    for (const Match &m : candidates) {
        if (detUsed[m.detIdx] || gaiaUsed[m.gaiaIdx])
            continue;
        detUsed[m.detIdx] = true;
        gaiaUsed[m.gaiaIdx] = true;
        result.push_back(m);
    }
    return result;
}

// One pooled star observation: a detected star matched to a Gaia catalog
// star in one sub, expressed as a pixel offset from the shared reference
// CRPIX and a tangent-plane residual (in mas) between the linear-WCS
// prediction and Gaia's epoch-propagated truth.
struct Observation {
    double uPx = 0.0;
    double vPx = 0.0;
    // The residual actually fed to the shared distortion-polynomial fit --
    // equal to (dxiRawMas, detaRawMas) unless EquipmentCalibrationOptions::
    // fitPerSubAffine removed this sub's own affine error from it first.
    double dxiMas = 0.0;
    double detaMas = 0.0;
    // Untouched, straight from this sub's own linear WCS -- never
    // corrected. What "before" means everywhere in the public result.
    double dxiRawMas = 0.0;
    double detaRawMas = 0.0;
    int gaiaIdx = -1; // index into the input gaiaCatalog, for the repeatability grouping
    int subIdx = -1;
};

Eigen::MatrixXd buildDesignMatrix(const QVector<Observation> &obs, int order, double pixelScaleNorm)
{
    const int nTerms = sipPolyTermCount(order);
    Eigen::MatrixXd M(obs.size(), nTerms);
    for (int i = 0; i < obs.size(); ++i) {
        QVector<double> row;
        row.reserve(nTerms);
        sipPolyTermsInto(obs[i].uPx / pixelScaleNorm, obs[i].vPx / pixelScaleNorm, order, row);
        for (int j = 0; j < nTerms; ++j)
            M(i, j) = row[j];
    }
    return M;
}

struct PolyFitResult {
    Eigen::VectorXd cx;
    Eigen::VectorXd cy;
    QVector<bool> keptMask;
    QVector<double> residualsMas;
    // Same residuals, split into their xi/eta tangent-plane components --
    // parallel to residualsMas (residualsMas[i] == hypot(residualXiMas[i],
    // residualEtaMas[i])) -- exposed for the axis-split RMS diagnostic and
    // the per-observation CSV export (ResidualObservation).
    QVector<double> residualXiMas;
    QVector<double> residualEtaMas;
    int nTerms = 0;
};

// Iteratively sigma-clipped weighted least-squares fit of the distortion
// polynomial against all pooled observations -- direct port of the Python
// prototype's fit_poly_sip(): a handful of clip/refit passes so a few bad
// cross-matches (blends, misidentifications) can't drag the polynomial
// around (see docs/equipment-profiling-spec.md section 4).
PolyFitResult fitPolySip(const QVector<Observation> &obs, int order, double pixelScaleNorm,
                          double clipSigma, int nIter)
{
    const Eigen::MatrixXd M = buildDesignMatrix(obs, order, pixelScaleNorm);
    const int n = obs.size();
    Eigen::VectorXd dxi(n), deta(n);
    for (int i = 0; i < n; ++i) {
        dxi(i) = obs[i].dxiMas;
        deta(i) = obs[i].detaMas;
    }

    QVector<bool> mask(n, true);
    Eigen::VectorXd cx, cy;
    QVector<double> res(n, 0.0);
    QVector<double> resXi(n, 0.0), resEta(n, 0.0);

    for (int iter = 0; iter < nIter; ++iter) {
        int nKept = 0;
        for (bool k : mask)
            nKept += k ? 1 : 0;
        Eigen::MatrixXd Mk(nKept, M.cols());
        Eigen::VectorXd dxiK(nKept), detaK(nKept);
        int r = 0;
        for (int i = 0; i < n; ++i) {
            if (mask[i]) {
                Mk.row(r) = M.row(i);
                dxiK(r) = dxi(i);
                detaK(r) = deta(i);
                ++r;
            }
        }
        cx = Mk.colPivHouseholderQr().solve(dxiK);
        cy = Mk.colPivHouseholderQr().solve(detaK);

        const Eigen::VectorXd predX = M * cx;
        const Eigen::VectorXd predY = M * cy;
        double sumSq = 0.0;
        for (int i = 0; i < n; ++i) {
            resXi[i] = dxi(i) - predX(i);
            resEta[i] = deta(i) - predY(i);
            res[i] = std::hypot(resXi[i], resEta[i]);
            if (mask[i])
                sumSq += res[i] * res[i];
        }
        const double sigma = nKept > 0 ? std::sqrt(sumSq / nKept) : 0.0;

        QVector<bool> newMask(n);
        bool changed = false;
        for (int i = 0; i < n; ++i) {
            newMask[i] = res[i] < clipSigma * sigma;
            if (newMask[i] != mask[i])
                changed = true;
        }
        mask = newMask;
        if (!changed)
            break;
    }

    PolyFitResult out;
    out.cx = cx;
    out.cy = cy;
    out.keptMask = mask;
    out.residualsMas = res;
    out.residualXiMas = resXi;
    out.residualEtaMas = resEta;
    out.nTerms = int(M.cols());
    return out;
}

double rmsOf(const QVector<double> &values, const QVector<bool> &mask)
{
    double sumSq = 0.0;
    int n = 0;
    for (int i = 0; i < values.size(); ++i) {
        if (mask.isEmpty() || mask[i]) {
            sumSq += values[i] * values[i];
            ++n;
        }
    }
    return n > 0 ? std::sqrt(sumSq / n) : std::numeric_limits<double>::quiet_NaN();
}

// Held-out cross-validation for one candidate order: repeated random 50/50
// train/test splits, plain (unclipped) lstsq fit on the train half each
// time, RMS evaluated on the held-out half -- matches the Python
// prototype's order-selection loop exactly (see
// docs/equipment-profiling-spec.md section 5 for why this, not in-sample
// RMS, is the number that must drive order selection).
void crossValidateOrder(const QVector<Observation> &obs, int order, double pixelScaleNorm,
                         int nSplits, unsigned seed, double *outMeanMas, double *outStdMas)
{
    const Eigen::MatrixXd M = buildDesignMatrix(obs, order, pixelScaleNorm);
    const int n = obs.size();
    Eigen::VectorXd dxi(n), deta(n);
    for (int i = 0; i < n; ++i) {
        dxi(i) = obs[i].dxiMas;
        deta(i) = obs[i].detaMas;
    }

    std::mt19937 rng(seed);
    QVector<double> rmsList;
    rmsList.reserve(nSplits);
    QVector<int> indices(n);
    for (int i = 0; i < n; ++i)
        indices[i] = i;

    for (int trial = 0; trial < nSplits; ++trial) {
        std::shuffle(indices.begin(), indices.end(), rng);
        const int half = n / 2;
        Eigen::MatrixXd Mtrain(half, M.cols());
        Eigen::VectorXd dxiTrain(half), detaTrain(half);
        for (int i = 0; i < half; ++i) {
            Mtrain.row(i) = M.row(indices[i]);
            dxiTrain(i) = dxi(indices[i]);
            detaTrain(i) = deta(indices[i]);
        }
        const Eigen::VectorXd cxt = Mtrain.colPivHouseholderQr().solve(dxiTrain);
        const Eigen::VectorXd cyt = Mtrain.colPivHouseholderQr().solve(detaTrain);

        const int nTest = n - half;
        double sumSq = 0.0;
        for (int i = half; i < n; ++i) {
            const Eigen::VectorXd row = M.row(indices[i]);
            const double predX = row.dot(cxt);
            const double predY = row.dot(cyt);
            const double dx = dxi(indices[i]) - predX;
            const double dy = deta(indices[i]) - predY;
            sumSq += dx * dx + dy * dy;
        }
        rmsList.push_back(nTest > 0 ? std::sqrt(sumSq / nTest) : std::numeric_limits<double>::quiet_NaN());
    }

    double mean = 0.0;
    for (double v : rmsList)
        mean += v;
    mean /= rmsList.size();
    double var = 0.0;
    for (double v : rmsList)
        var += (v - mean) * (v - mean);
    var /= rmsList.size();

    *outMeanMas = mean;
    *outStdMas = std::sqrt(var);
}

} // namespace

EquipmentCalibrationResult EquipmentCalibrator::calibrate(const QVector<CalibrationSub> &subs,
                                                            const QVector<GaiaStar> &gaiaCatalog,
                                                            const EquipmentCalibrationOptions &options)
{
    EquipmentCalibrationResult result;

    if (subs.isEmpty()) {
        result.errorMessage = QStringLiteral("no calibration subs given");
        return result;
    }
    if (gaiaCatalog.isEmpty()) {
        result.errorMessage = QStringLiteral("empty Gaia catalog");
        return result;
    }

    // Reference pixel: the chip center (from the first sub's image
    // dimensions), held fixed for every sub's pixel offsets -- NOT any
    // individual sub's own solved CRPIX. Distortion is a property of the
    // detector position, not of where the telescope happened to point that
    // sub, and a solved CRPIX is wherever the plate solver's tangent-point
    // fit happened to land (which can itself be off-center depending on
    // where the matched stars were concentrated) -- the chip center is the
    // one fixed, physically-meaningful point every sub shares regardless of
    // dithering, pointing, or solve quality. Falls back to the first sub's
    // solved CRPIX only if its .wcs sidecar doesn't carry image dimensions.
    // See docs/equipment-profiling-spec.md section 3 step 2 / the pooling
    // rationale in sip_fit_pooled.py.
    const PlateSolveResult firstWcs = PlateSolver::readWcsFile(subs.first().wcsPath);
    if (!firstWcs.solved) {
        result.errorMessage =
            QStringLiteral("failed to read reference WCS %1: %2").arg(subs.first().wcsPath, firstWcs.errorMessage);
        return result;
    }
    const bool haveImageDims = firstWcs.imageWidthPx > 0 && firstWcs.imageHeightPx > 0;
    const double refCrpix1 = haveImageDims ? (firstWcs.imageWidthPx + 1.0) / 2.0 : firstWcs.crpix1;
    const double refCrpix2 = haveImageDims ? (firstWcs.imageHeightPx + 1.0) / 2.0 : firstWcs.crpix2;

    double pixelScaleNorm = options.pixelScaleNorm;
    if (pixelScaleNorm <= 0.0)
        pixelScaleNorm = std::max(firstWcs.imageWidthPx, firstWcs.imageHeightPx) / 2.0;

    QVector<Observation> pooled;
    // gaiaIdx -> list of (dxiMas, detaMas) observations, for the internal
    // repeatability diagnostic (section 6) -- independent of the fit below.
    QMap<int, QVector<QPair<double, double>>> byStar;

    QVector<SubResidualResult> subResults(subs.size());
    for (int i = 0; i < subs.size(); ++i) {
        subResults[i].subIndex = i;
        subResults[i].imagePath = subs[i].imagePath;
    }
    QVector<SubAffineFit> subAffineFits; // one entry per sub actually processed, only if fitPerSubAffine

    int nSubsUsed = 0;
    for (int subIdx = 0; subIdx < subs.size(); ++subIdx) {
        const CalibrationSub &sub = subs[subIdx];

        const FitsImageData image = FitsImage::load(sub.imagePath);
        if (!image.loaded) {
            subResults[subIdx].skipReason =
                image.errorMessage.isEmpty() ? QStringLiteral("failed to load FITS image")
                                              : QStringLiteral("failed to load FITS image: %1").arg(image.errorMessage);
            continue; // skip unreadable subs rather than failing the whole calibration
        }
        const PlateSolveResult wcs = PlateSolver::readWcsFile(sub.wcsPath);
        if (!wcs.solved) {
            subResults[subIdx].skipReason = QStringLiteral("failed to read WCS: %1").arg(wcs.errorMessage);
            continue;
        }

        const LinearWcs linearWcs(wcs.crval1Deg, wcs.crval2Deg, wcs.crpix1, wcs.crpix2, wcs.cd11,
                                   wcs.cd12, wcs.cd21, wcs.cd22);
        if (!linearWcs.isValid()) {
            subResults[subIdx].skipReason =
                QStringLiteral("invalid linear WCS: %1").arg(linearWcs.errorMessage());
            continue;
        }

        const double epochJyear = std::isfinite(image.dateObsJyear) ? image.dateObsJyear
                                                                      : std::numeric_limits<double>::quiet_NaN();
        if (!std::isfinite(epochJyear)) {
            subResults[subIdx].skipReason = QStringLiteral("no usable DATE-OBS");
            continue; // can't propagate Gaia without knowing when this sub was taken
        }

        const QVector<DetectedStar> detected = StarDetector::detect(image, options.detection);
        if (detected.isEmpty()) {
            subResults[subIdx].skipReason = QStringLiteral("no stars detected");
            continue;
        }

        QVector<double> detRa(detected.size()), detDec(detected.size());
        for (int i = 0; i < detected.size(); ++i) {
            double ra = 0.0, dec = 0.0;
            // Detected coordinates are 0-indexed (FitsImageData convention);
            // WCS pixel coordinates are FITS 1-indexed.
            if (!linearWcs.pixToWorld(detected[i].x + 1.0, detected[i].y + 1.0, &ra, &dec)) {
                ra = std::numeric_limits<double>::quiet_NaN();
                dec = std::numeric_limits<double>::quiet_NaN();
            }
            detRa[i] = ra;
            detDec[i] = dec;
        }

        QVector<double> gaiaRa(gaiaCatalog.size()), gaiaDec(gaiaCatalog.size());
        for (int i = 0; i < gaiaCatalog.size(); ++i) {
            const SpaceMotion::Position pos = SpaceMotion::propagate(gaiaCatalog[i], epochJyear);
            gaiaRa[i] = pos.raDeg;
            gaiaDec[i] = pos.decDeg;
        }

        const QVector<Match> matches =
            crossMatch(detRa, detDec, gaiaRa, gaiaDec, options.matchToleranceArcsec);
        if (matches.isEmpty())
            subResults[subIdx].skipReason = QStringLiteral("no cross-matches to Gaia within tolerance");

        // Collect this sub's own observations first -- its affine fit below
        // needs the full set before it can be fit.
        QVector<Observation> subObs;
        subObs.reserve(matches.size());
        for (const Match &m : matches) {
            if (!std::isfinite(detRa[m.detIdx]) || !std::isfinite(detDec[m.detIdx]))
                continue;
            const double raTrue = gaiaRa[m.gaiaIdx];
            const double decTrue = gaiaDec[m.gaiaIdx];
            const double cosDec = std::cos(decTrue * M_PI / 180.0);
            const double dxiMas = (raTrue - detRa[m.detIdx]) * cosDec * 3.6e6;
            const double detaMas = (decTrue - detDec[m.detIdx]) * 3.6e6;

            Observation obs;
            obs.uPx = (detected[m.detIdx].x + 1.0) - refCrpix1;
            obs.vPx = (detected[m.detIdx].y + 1.0) - refCrpix2;
            obs.dxiRawMas = dxiMas;
            obs.detaRawMas = detaMas;
            obs.dxiMas = dxiMas;   // corrected below if fitPerSubAffine
            obs.detaMas = detaMas;
            obs.gaiaIdx = m.gaiaIdx;
            obs.subIdx = subIdx;
            subObs.push_back(obs);
        }

        // Fit and remove this sub's own affine (translation + rotation +
        // scale + shear) error before pooling -- see
        // EquipmentCalibrationOptions::fitPerSubAffine and SubAffineFit.
        // Each sub was plate-solved independently, so each carries its own
        // small solving error on top of the rig's actual optical
        // distortion; a shared polynomial fit across all subs structurally
        // can't remove an error that varies sub to sub, so it's removed
        // here first, leaving the shared fit only what's genuinely common
        // to every sub.
        if (options.fitPerSubAffine) {
            SubAffineFit affineResult;
            affineResult.subIndex = subIdx;
            affineResult.nObservations = subObs.size();
            const double minObsForSubAffine =
                options.minObservationsPerParameter * 2.0 * sipPolyTermCount(1);
            if (subObs.size() >= minObsForSubAffine) {
                const PolyFitResult subFit = fitPolySip(subObs, 1, pixelScaleNorm, options.sigmaClip,
                                                          options.sigmaClipIterations);
                int nKept = 0;
                for (bool k : subFit.keptMask)
                    nKept += k ? 1 : 0;

                QVector<double> subBefore(subObs.size());
                for (int i = 0; i < subObs.size(); ++i)
                    subBefore[i] = std::hypot(subObs[i].dxiRawMas, subObs[i].detaRawMas);

                affineResult.fitted = true;
                affineResult.nKept = nKept;
                affineResult.rmsBeforeMas = rmsOf(subBefore, {});
                affineResult.rmsAfterMas = rmsOf(subFit.residualsMas, subFit.keptMask);

                // Decompose into rotation/scale/shear/translation using
                // this sub's own plate scale, so the numbers are physically
                // meaningful (deg, %, mas) rather than raw polynomial
                // coefficients. Term order from sipPolyTermsInto(order=1)
                // is [1, v, u], and coefficients come out in mas per
                // *normalized* pixel (divided by pixelScaleNorm), so undo
                // that normalization before comparing against a plate scale
                // in mas per raw pixel.
                //
                // The rotation/scale/shear split below looks like it should
                // be the textbook small-angle 2x2 decomposition, but isn't
                // quite: CD matrices carry the standard astronomical RA
                // flip (cd11 negative, cd22 positive -- RA increases as
                // pixel x decreases), so the *tangent-plane* map has a
                // built-in reflection between its two axes. Working through
                // what a pure position-angle error actually does to
                // (dxi, deta) under that reflection (rather than assuming
                // an unreflected/isotropic frame) gives: a rotation shows
                // up as a2 ~= b1 (same sign, not opposite), an isotropic
                // scale error as a1 ~= -b2 (opposite sign, not same), and a
                // non-rotational shear as b1 ~= -a2. That's the swap
                // applied here.
                const double plateScaleMasPerPx =
                    std::sqrt(std::abs(wcs.cd11 * wcs.cd22 - wcs.cd12 * wcs.cd21)) * 3.6e6;
                const double a0 = subFit.cx(0);
                const double a2 = subFit.cx(1) / pixelScaleNorm; // dxi's dependence on v
                const double a1 = subFit.cx(2) / pixelScaleNorm; // dxi's dependence on u
                const double b0 = subFit.cy(0);
                const double b2 = subFit.cy(1) / pixelScaleNorm; // deta's dependence on v
                const double b1 = subFit.cy(2) / pixelScaleNorm; // deta's dependence on u
                if (plateScaleMasPerPx > 0.0) {
                    affineResult.rotationDeg =
                        (a2 + b1) / (2.0 * plateScaleMasPerPx) * (180.0 / M_PI);
                    affineResult.scaleErrorPct = (a1 - b2) / (2.0 * plateScaleMasPerPx) * 100.0;
                    affineResult.shearDeg = (b1 - a2) / (2.0 * plateScaleMasPerPx) * (180.0 / M_PI);
                }
                affineResult.translationXiMas = a0;
                affineResult.translationEtaMas = b0;

                // Apply the fit to every one of this sub's observations
                // (not just the ones it kept -- a star clipped from this
                // sub's own affine fit still deserves the correction before
                // it goes into the shared pooled fit, which does its own
                // independent sigma-clipping).
                QVector<double> termsRow;
                for (int i = 0; i < subObs.size(); ++i) {
                    termsRow.clear();
                    sipPolyTermsInto(subObs[i].uPx / pixelScaleNorm, subObs[i].vPx / pixelScaleNorm, 1,
                                      termsRow);
                    double predXi = 0.0, predEta = 0.0;
                    for (int t = 0; t < termsRow.size(); ++t) {
                        predXi += termsRow[t] * subFit.cx(t);
                        predEta += termsRow[t] * subFit.cy(t);
                    }
                    subObs[i].dxiMas = subObs[i].dxiRawMas - predXi;
                    subObs[i].detaMas = subObs[i].detaRawMas - predEta;
                }
            } else {
                affineResult.skipReason =
                    QStringLiteral("too few observations for its own affine fit (%1 < %2)")
                        .arg(subObs.size())
                        .arg(minObsForSubAffine, 0, 'f', 0);
            }
            subAffineFits.push_back(affineResult);
        }

        for (const Observation &o : subObs) {
            pooled.push_back(o);
            byStar[o.gaiaIdx].push_back({o.dxiMas, o.detaMas});
        }

        ++nSubsUsed;
    }

    if (pooled.isEmpty()) {
        result.errorMessage = QStringLiteral(
            "no star-observations survived detection + cross-matching across any sub");
        return result;
    }

    result.nSubsUsed = nSubsUsed;
    result.nStarObservations = pooled.size();
    result.subAffineFits = subAffineFits;

    QVector<double> beforeResid;
    beforeResid.reserve(pooled.size());
    for (const Observation &o : pooled)
        beforeResid.push_back(std::hypot(o.dxiRawMas, o.detaRawMas));
    result.rmsBeforeMas = rmsOf(beforeResid, {});

    // Axis-split "before" RMS -- xi (RA-like) and eta (Dec-like) separately,
    // straight from the pooled tangent-plane offsets, no fit involved.
    {
        QVector<double> xiVals, etaVals;
        xiVals.reserve(pooled.size());
        etaVals.reserve(pooled.size());
        for (const Observation &o : pooled) {
            xiVals.push_back(o.dxiRawMas);
            etaVals.push_back(o.detaRawMas);
        }
        result.rmsBeforeXiMas = rmsOf(xiVals, {});
        result.rmsBeforeEtaMas = rmsOf(etaVals, {});
    }

    // RMS after each sub's own affine correction (if fitPerSubAffine is on)
    // but before the shared distortion polynomial -- shows how much of the
    // "before" number was really per-sub solving noise vs. what's actually
    // left for the shared fit to explain. Equal to the before numbers above
    // when fitPerSubAffine is off, since dxiMas/detaMas == dxiRawMas/
    // detaRawMas in that case.
    {
        QVector<double> combined, xiVals, etaVals;
        combined.reserve(pooled.size());
        xiVals.reserve(pooled.size());
        etaVals.reserve(pooled.size());
        for (const Observation &o : pooled) {
            combined.push_back(std::hypot(o.dxiMas, o.detaMas));
            xiVals.push_back(o.dxiMas);
            etaVals.push_back(o.detaMas);
        }
        result.rmsAfterSubAffineMas = rmsOf(combined, {});
        result.rmsAfterSubAffineXiMas = rmsOf(xiVals, {});
        result.rmsAfterSubAffineEtaMas = rmsOf(etaVals, {});
    }

    // Per-sub "before" RMS and observation counts, for the residual report
    // (SubResidualResult) -- independent of which order ends up chosen.
    {
        QVector<double> subSumSqBefore(subs.size(), 0.0);
        QVector<int> subCountBefore(subs.size(), 0);
        for (int i = 0; i < pooled.size(); ++i) {
            const int si = pooled[i].subIdx;
            subSumSqBefore[si] += beforeResid[i] * beforeResid[i];
            subCountBefore[si] += 1;
        }
        for (int i = 0; i < subs.size(); ++i) {
            subResults[i].nObservations = subCountBefore[i];
            if (subCountBefore[i] > 0)
                subResults[i].rmsBeforeMas = std::sqrt(subSumSqBefore[i] / subCountBefore[i]);
        }
    }

    // --- Order selection via cross-validation (section 5) ---
    QVector<PolyFitResult> fitsByOrder;
    for (int order : options.candidateOrders) {
        const int nTerms = sipPolyTermCount(order);
        const double minObs = options.minObservationsPerParameter * 2.0 * nTerms;
        if (pooled.size() < minObs)
            continue; // not enough data to trust this order at all -- don't offer it as a candidate

        const PolyFitResult fit =
            fitPolySip(pooled, order, pixelScaleNorm, options.sigmaClip, options.sigmaClipIterations);
        const double inSampleRms = rmsOf(fit.residualsMas, fit.keptMask);

        double heldoutMean = 0.0, heldoutStd = 0.0;
        crossValidateOrder(pooled, order, pixelScaleNorm, options.crossValidationSplits,
                            options.crossValidationSeed, &heldoutMean, &heldoutStd);

        OrderCandidateResult candidate;
        candidate.order = order;
        candidate.nTermsPerAxis = nTerms;
        int nKept = 0;
        for (bool k : fit.keptMask)
            nKept += k ? 1 : 0;
        candidate.nKeptInSample = nKept;
        candidate.inSampleRmsMas = inSampleRms;
        candidate.heldoutRmsMeanMas = heldoutMean;
        candidate.heldoutRmsStdMas = heldoutStd;

        result.orderCandidates.push_back(candidate);
        fitsByOrder.push_back(fit);
    }

    if (result.orderCandidates.isEmpty()) {
        result.errorMessage = QStringLiteral(
            "not enough pooled star-observations (%1) to trust even the lowest candidate order -- "
            "use more subs, a lower minimum order, or check that detection/cross-matching is "
            "actually finding stars")
                                   .arg(pooled.size());
        return result;
    }

    // Pick the lowest order at which held-out RMS stabilizes: stop as soon
    // as the next order's improvement is no larger than that next order's
    // own held-out scatter (i.e. the "improvement" is consistent with
    // noise), or flag overfitting outright if the next order is actually
    // worse by more than its scatter.
    int chosenIndex = result.orderCandidates.size() - 1;
    for (int i = 0; i < result.orderCandidates.size() - 1; ++i) {
        const OrderCandidateResult &cur = result.orderCandidates[i];
        const OrderCandidateResult &next = result.orderCandidates[i + 1];
        const double improvement = cur.heldoutRmsMeanMas - next.heldoutRmsMeanMas;
        if (next.heldoutRmsMeanMas > cur.heldoutRmsMeanMas + next.heldoutRmsStdMas) {
            result.overfittingWarning = true;
            result.overfittingWarningText =
                QStringLiteral("held-out RMS got worse going from order %1 to order %2 (%3 -> %4 "
                                "mas) -- higher orders are overfitting past this point")
                    .arg(cur.order)
                    .arg(next.order)
                    .arg(cur.heldoutRmsMeanMas, 0, 'f', 1)
                    .arg(next.heldoutRmsMeanMas, 0, 'f', 1);
            chosenIndex = i;
            break;
        }
        if (improvement < next.heldoutRmsStdMas) {
            chosenIndex = i;
            break;
        }
    }
    result.chosenOrder = result.orderCandidates[chosenIndex].order;
    const PolyFitResult &chosenFit = fitsByOrder[chosenIndex];

    // Per-sub "after" RMS at the chosen order, over each sub's kept (not
    // sigma-clipped) observations -- completes the SubResidualResult report.
    {
        QVector<double> subSumSqAfter(subs.size(), 0.0);
        QVector<int> subCountKept(subs.size(), 0);
        for (int i = 0; i < pooled.size(); ++i) {
            if (!chosenFit.keptMask[i])
                continue;
            const int si = pooled[i].subIdx;
            subSumSqAfter[si] += chosenFit.residualsMas[i] * chosenFit.residualsMas[i];
            subCountKept[si] += 1;
        }
        for (int i = 0; i < subs.size(); ++i) {
            subResults[i].nKept = subCountKept[i];
            if (subCountKept[i] > 0)
                subResults[i].rmsAfterMas = std::sqrt(subSumSqAfter[i] / subCountKept[i]);
        }
    }
    result.subResiduals = subResults;

    // Axis-split "after" RMS at the chosen order, over kept observations
    // only -- same split as rmsBeforeXiMas/EtaMas, so the two are directly
    // comparable per axis.
    {
        QVector<double> xiKept, etaKept;
        for (int i = 0; i < pooled.size(); ++i) {
            if (!chosenFit.keptMask[i])
                continue;
            xiKept.push_back(chosenFit.residualXiMas[i]);
            etaKept.push_back(chosenFit.residualEtaMas[i]);
        }
        result.rmsAfterXiMas = rmsOf(xiKept, {});
        result.rmsAfterEtaMas = rmsOf(etaKept, {});
    }

    // Per-observation export (ResidualObservation) -- one row per pooled
    // star observation, before and after the chosen-order fit, for spatial/
    // per-sub inspection (see the struct comment in EquipmentCalibrator.h).
    result.observations.reserve(pooled.size());
    for (int i = 0; i < pooled.size(); ++i) {
        ResidualObservation ro;
        ro.subIndex = pooled[i].subIdx;
        ro.pixelUOffset = pooled[i].uPx;
        ro.pixelVOffset = pooled[i].vPx;
        ro.radiusPx = std::hypot(pooled[i].uPx, pooled[i].vPx);
        ro.dxiBeforeMas = pooled[i].dxiRawMas;
        ro.detaBeforeMas = pooled[i].detaRawMas;
        ro.dxiAfterMas = chosenFit.residualXiMas[i];
        ro.detaAfterMas = chosenFit.residualEtaMas[i];
        ro.keptInFit = chosenFit.keptMask[i];
        result.observations.push_back(ro);
    }

    // --- Internal repeatability (section 6) ---
    // byStar was populated from each sub's post-affine-correction residual
    // (dxiMas/detaMas, not dxiRawMas/detaRawMas) when fitPerSubAffine is on
    // -- otherwise a star's sub-to-sub scatter here would be inflated by
    // each sub's own rotation/scale solving error on top of genuine
    // measurement noise, understating how good the actual centroiding is
    // and misleading the limiting_factor call below.
    const int minObsForRepeatability = std::max(3, nSubsUsed / 2);
    QVector<double> internalScatter;
    QVector<double> internalScatterXi, internalScatterEta;
    for (auto it = byStar.constBegin(); it != byStar.constEnd(); ++it) {
        const QVector<QPair<double, double>> &obsList = it.value();
        if (obsList.size() < minObsForRepeatability)
            continue;
        double meanDxi = 0.0, meanDeta = 0.0;
        for (const auto &p : obsList) {
            meanDxi += p.first;
            meanDeta += p.second;
        }
        meanDxi /= obsList.size();
        meanDeta /= obsList.size();
        double varDxi = 0.0, varDeta = 0.0;
        for (const auto &p : obsList) {
            varDxi += (p.first - meanDxi) * (p.first - meanDxi);
            varDeta += (p.second - meanDeta) * (p.second - meanDeta);
        }
        varDxi /= obsList.size();
        varDeta /= obsList.size();
        internalScatter.push_back(std::hypot(std::sqrt(varDxi), std::sqrt(varDeta)));
        internalScatterXi.push_back(std::sqrt(varDxi));
        internalScatterEta.push_back(std::sqrt(varDeta));
    }
    result.nStarsForRepeatability = internalScatter.size();
    result.internalRepeatabilityXiMas = rmsOf(internalScatterXi, {});
    result.internalRepeatabilityEtaMas = rmsOf(internalScatterEta, {});

    double internalMedianMas = std::numeric_limits<double>::quiet_NaN();
    double internalRmsMas = std::numeric_limits<double>::quiet_NaN();
    if (!internalScatter.isEmpty()) {
        QVector<double> sorted = internalScatter;
        std::sort(sorted.begin(), sorted.end());
        const int n = sorted.size();
        internalMedianMas = (n % 2 == 0) ? (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0 : sorted[n / 2];
        internalRmsMas = rmsOf(internalScatter, {});
    }

    // --- Assemble the profile ---
    EquipmentProfile &profile = result.profile;
    profile.nSubsUsed = nSubsUsed;
    profile.nStarsMatched = pooled.size();
    profile.detectionThresholdSigma = options.detection.thresholdSigma;
    profile.crpix1 = refCrpix1;
    profile.crpix2 = refCrpix2;
    profile.pixelScaleNorm = pixelScaleNorm;
    profile.polyOrderChosen = result.chosenOrder;
    profile.polyCoeffsXi.resize(chosenFit.cx.size());
    for (int i = 0; i < chosenFit.cx.size(); ++i)
        profile.polyCoeffsXi[i] = chosenFit.cx(i);
    profile.polyCoeffsEta.resize(chosenFit.cy.size());
    for (int i = 0; i < chosenFit.cy.size(); ++i)
        profile.polyCoeffsEta[i] = chosenFit.cy(i);
    profile.rmsBeforeMas = result.rmsBeforeMas;
    profile.rmsAfterInSampleMas = result.orderCandidates[chosenIndex].inSampleRmsMas;
    profile.rmsAfterHeldoutMas = result.orderCandidates[chosenIndex].heldoutRmsMeanMas;
    profile.rmsAfterHeldoutStdMas = result.orderCandidates[chosenIndex].heldoutRmsStdMas;
    profile.internalRepeatabilityMedianMas = internalMedianMas;
    profile.internalRepeatabilityRmsMas = internalRmsMas;

    // limiting_factor: this implementation only auto-diagnoses the one
    // comparison the spec calls out as required (section 6) -- internal
    // repeatability vs. post-fit held-out residual. It deliberately does
    // NOT claim to distinguish catalog_depth/detection_noise automatically
    // (the spec discusses those as manual investigations a user can run,
    // not something this single calibration run's numbers can tell apart
    // on their own).
    if (!std::isfinite(internalRmsMas)) {
        profile.limitingFactor = QStringLiteral("unclear");
    } else if (profile.rmsAfterHeldoutMas <= internalRmsMas * 1.3) {
        profile.limitingFactor = QStringLiteral("measurement_precision");
    } else {
        profile.limitingFactor = QStringLiteral("unclear");
    }

    result.ok = true;
    return result;
}

} // namespace epochfrom
