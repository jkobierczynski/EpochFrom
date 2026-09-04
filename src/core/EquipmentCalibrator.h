#pragma once

#include "EquipmentProfile.h"
#include "GaiaCatalog.h"
#include "StarDetector.h"

#include <QString>
#include <QVector>
#include <limits>

namespace epochfrom {

// One calibration input: a light frame plus its already-solved .wcs sidecar
// (same split PlateSolver uses -- pixel data and DATE-OBS live in the FITS
// image, the linear WCS terms live in the .wcs file). Solve subs
// individually before calibrating; see docs/equipment-profiling-spec.md
// section 3, step 2, for why (dithering between subs breaks the "reuse one
// sub's WCS for all of them" shortcut).
struct CalibrationSub {
    QString imagePath;
    QString wcsPath;
};

struct EquipmentCalibrationOptions {
    StarDetectionOptions detection;
    double matchToleranceArcsec = 3.0;
    QVector<int> candidateOrders = {1, 2, 3, 4, 5, 6};
    int crossValidationSplits = 20;
    double sigmaClip = 3.5;
    int sigmaClipIterations = 5;
    // Normalization constant the polynomial's u/v pixel-offset terms are
    // divided by before raising to a power -- see SipPolynomial.h. 2500px
    // matches what the prototype validated against a ~4656px-wide sensor
    // (roughly half the long axis); pass 0 to have the calibrator pick it
    // automatically as half of max(imageWidth, imageHeight) from the first
    // sub instead.
    double pixelScaleNorm = 2500.0;
    unsigned crossValidationSeed = 0;
    // A polynomial order is only offered as a candidate if the pooled star
    // observation count is at least this many times its parameter count
    // (2 * (order+1)(order+2)/2, both axes) -- guards against silently
    // fitting (and recommending) an overfit model from a too-small
    // calibration session, per the spec's open item on this.
    double minObservationsPerParameter = 5.0;
    // Fit and remove a per-sub affine (translation + rotation + scale +
    // shear) correction, independently for each sub, before pooling for the
    // shared distortion polynomial below. Each sub was plate-solved on its
    // own, so each carries its own small solving error (a position angle or
    // scale that's slightly off from truth) on top of whatever the rig's
    // actual optical distortion is -- pooling all subs' raw residuals
    // straight into one shared polynomial conflates the two, and a single
    // shared function of pixel position structurally can't remove an error
    // that varies sub to sub. Fitting it away per sub first leaves the
    // shared polynomial only what's genuinely common across every sub: the
    // optics. A sub needs at least minObservationsPerParameter * 2 *
    // (order-1 term count) observations to get its own affine fit (same
    // density rule as the shared polynomial's order gating); subs with
    // fewer are pooled with their raw (uncorrected) residuals instead. See
    // SubAffineFit below.
    bool fitPerSubAffine = true;
};

struct OrderCandidateResult {
    int order = 0;
    int nTermsPerAxis = 0;
    int nKeptInSample = 0;
    double inSampleRmsMas = 0.0;
    double heldoutRmsMeanMas = 0.0;
    double heldoutRmsStdMas = 0.0;
};

// One sub's own affine (translation + rotation + scale + shear) fit,
// removed from its observations before they're pooled for the shared
// distortion polynomial -- see EquipmentCalibrationOptions::fitPerSubAffine.
// rotationDeg/scaleErrorPct/shearDeg/translation are the standard 2D-affine
// decomposition of the fit, converted out of the raw polynomial
// coefficients using this sub's own plate scale (so e.g. rotationDeg is
// directly "how far off was this sub's solved position angle from what
// actually matches Gaia", independent of pixel scale or image size).
// Comparing this across subs is the direct answer to "is the swirl/offset I
// see in the pooled 'before' view one property of the rig, or is it really
// N different small per-sub solving errors that happen to look similar
// overlaid" -- if rotationDeg (or scaleErrorPct) varies a lot sub to sub,
// it's the latter, and no shared spatial polynomial could ever have fixed
// it.
struct SubAffineFit {
    int subIndex = 0;
    bool fitted = false; // false if this sub had too few observations to fit its own affine
    QString skipReason;  // set when fitted == false
    int nObservations = 0;
    int nKept = 0; // survived this sub's own sigma-clipping
    double rotationDeg = std::numeric_limits<double>::quiet_NaN();       // fitted position-angle offset from this sub's own solved WCS
    double scaleErrorPct = std::numeric_limits<double>::quiet_NaN();     // fractional plate-scale error, as a percentage
    double shearDeg = std::numeric_limits<double>::quiet_NaN();          // non-rotational skew (off-diagonal, symmetric part)
    double translationXiMas = std::numeric_limits<double>::quiet_NaN();  // fitted zero-point offset, xi (RA-like)
    double translationEtaMas = std::numeric_limits<double>::quiet_NaN(); // fitted zero-point offset, eta (Dec-like)
    double rmsBeforeMas = std::numeric_limits<double>::quiet_NaN(); // this sub's own RMS, straight from its solved WCS
    double rmsAfterMas = std::numeric_limits<double>::quiet_NaN();  // this sub's own RMS, after only its own affine fit (in-sample)
};

// Per-sub breakdown of how much each input sub contributed to the pooled
// fit and how well it ended up matching Gaia, at the chosen order -- lets a
// caller tell whether a large aggregate residual (see the internal-vs-
// held-out gap in EquipmentCalibrationResult) is spread evenly across the
// session or driven by a handful of bad subs (a meridian flip, a guiding
// hiccup, clouds), which the pooled numbers alone can't distinguish.
struct SubResidualResult {
    int subIndex = 0;   // index into the `subs` vector passed to calibrate()
    QString imagePath;  // subs[subIndex].imagePath, for display
    // Non-empty if this sub contributed zero observations to the pooled
    // fit -- e.g. the image or WCS failed to load, DATE-OBS was missing or
    // unparseable, no stars were detected, or nothing cross-matched to
    // Gaia within tolerance. nObservations/nKept/rmsBeforeMas/rmsAfterMas
    // are all left at their default (0 / NaN) when this is set.
    QString skipReason;
    int nObservations = 0; // cross-matched observations pooled from this sub
    int nKept = 0;          // of those, kept (not sigma-clipped) in the chosen-order fit
    double rmsBeforeMas = std::numeric_limits<double>::quiet_NaN(); // linear WCS only, this sub's observations
    double rmsAfterMas = std::numeric_limits<double>::quiet_NaN();  // chosen-order fit, this sub's kept observations
};

// One pooled star observation, exposed for external inspection (e.g. a CSV
// export to plot spatially) -- a caller can compare the "raw" distortion
// (before, straight from the linear WCS) against what's left after the
// chosen-order fit, per star per sub, and against pixel position/radius --
// which is what actually distinguishes a radially-symmetric cause (field
// curvature: correlates with distance from the reference pixel, same
// pattern in every sub) from a per-exposure cause (tracking/guiding jitter:
// shows up as scatter that's worse in some subs than others, not tied to
// where on the sensor a star happened to land). ξ (xi) is the RA-like
// tangent-plane axis, η (eta) the Dec-like one -- same convention as
// everywhere else in this pipeline. pixelUOffset/pixelVOffset are relative
// to a fixed detector-frame reference pixel shared by every sub (the first
// sub's chip center, not any one sub's own solved CRPIX -- see the
// refCrpix1/refCrpix2 comment in EquipmentCalibrator.cpp), so a given
// star's position in this coordinate system means the same physical spot on
// the sensor regardless of which sub it was observed in, or how that sub
// happened to be dithered/pointed. dxiAfterMas/detaAfterMas are after BOTH
// corrections when fitPerSubAffine is on: that sub's own affine fit, then
// the shared distortion polynomial -- i.e. the full correction chain, same
// as what a future dating run would apply.
struct ResidualObservation {
    int subIndex = 0;
    double pixelUOffset = 0.0; // pixel x, offset from the shared reference pixel (see struct comment)
    double pixelVOffset = 0.0; // pixel y, offset from the shared reference pixel
    double radiusPx = 0.0;     // hypot(pixelUOffset, pixelVOffset) -- distance from the reference pixel
    double dxiBeforeMas = 0.0;  // RA-direction (xi) residual, linear WCS only, before any correction
    double detaBeforeMas = 0.0; // Dec-direction (eta) residual, linear WCS only, before any correction
    double dxiAfterMas = 0.0;   // RA-direction (xi) residual, after per-sub affine (if on) + the chosen-order fit
    double detaAfterMas = 0.0;  // Dec-direction (eta) residual, after per-sub affine (if on) + the chosen-order fit
    bool keptInFit = false;     // survived sigma-clipping in the chosen-order fit
};

struct EquipmentCalibrationResult {
    bool ok = false;
    QString errorMessage;

    int nSubsUsed = 0;
    int nStarObservations = 0; // pooled, across all subs, after per-sub cross-matching
    double rmsBeforeMas = std::numeric_limits<double>::quiet_NaN(); // pure linear WCS, no correction
    // Same "before" residual, split into its RA-like (xi) and Dec-like
    // (eta) tangent-plane components -- lets a caller see whether the
    // uncorrected distortion is lopsided between the two axes (as pure
    // tracking/guiding error along one mount axis typically would be)
    // rather than only the combined magnitude.
    double rmsBeforeXiMas = std::numeric_limits<double>::quiet_NaN();
    double rmsBeforeEtaMas = std::numeric_limits<double>::quiet_NaN();

    // Same split, but after the chosen-order fit, over its kept
    // (not sigma-clipped) observations.
    double rmsAfterXiMas = std::numeric_limits<double>::quiet_NaN();
    double rmsAfterEtaMas = std::numeric_limits<double>::quiet_NaN();

    // Same split for the internal (sub-to-sub, no-Gaia) repeatability
    // diagnostic -- aggregate RMS, across all repeatability-eligible stars,
    // of each star's own xi/eta scatter across the subs it appears in.
    double internalRepeatabilityXiMas = std::numeric_limits<double>::quiet_NaN();
    double internalRepeatabilityEtaMas = std::numeric_limits<double>::quiet_NaN();

    // One entry per options.candidateOrders that had enough observations to
    // be tested at all (see minObservationsPerParameter) -- in the order
    // they were tried.
    QVector<OrderCandidateResult> orderCandidates;
    int chosenOrder = -1; // index into candidateOrders that was actually tried and chosen, or -1
    bool overfittingWarning = false;
    QString overfittingWarningText;

    int nStarsForRepeatability = 0;

    // One entry per input sub, in the same order as `subs` -- populated
    // only when options.fitPerSubAffine is true (default). See
    // SubAffineFit above.
    QVector<SubAffineFit> subAffineFits;
    // RMS combined/xi/eta, computed on the pooled residuals after each
    // sub's own affine correction (if fitPerSubAffine is on) but BEFORE the
    // shared distortion polynomial -- lets a caller see how much the
    // per-sub step alone accounted for, separately from the shared fit that
    // follows it. Equal to rmsBeforeMas/XiMas/EtaMas when fitPerSubAffine is
    // off.
    double rmsAfterSubAffineMas = std::numeric_limits<double>::quiet_NaN();
    double rmsAfterSubAffineXiMas = std::numeric_limits<double>::quiet_NaN();
    double rmsAfterSubAffineEtaMas = std::numeric_limits<double>::quiet_NaN();

    // One entry per input sub, in the same order as `subs` -- see
    // SubResidualResult above.
    QVector<SubResidualResult> subResiduals;

    // One entry per pooled star observation -- see ResidualObservation
    // above. Populated whenever the fit succeeds, regardless of whether the
    // caller asked for a CSV export of it.
    QVector<ResidualObservation> observations;

    // Fully populated except for the purely descriptive user-metadata
    // fields (label, telescope/camera description, corrector type, filter,
    // valid date range) -- the caller fills those in from what the user
    // told it before saving.
    EquipmentProfile profile;
};

// Runs the full single-epoch equipment-profiling pipeline (see
// docs/equipment-profiling-spec.md section 3): detect stars in each sub,
// cross-match against `gaiaCatalog` propagated to each sub's own DATE-OBS,
// pool matches across subs, fit a 2D polynomial distortion model at each
// candidate order with held-out cross-validation, pick the order at which
// held-out RMS stabilizes, and compute the internal (sub-to-sub, no-Gaia)
// repeatability diagnostic that separates "centroiding is the ceiling" from
// "there's a real residual distortion" (section 6).
class EquipmentCalibrator {
public:
    static EquipmentCalibrationResult calibrate(const QVector<CalibrationSub> &subs,
                                                  const QVector<GaiaStar> &gaiaCatalog,
                                                  const EquipmentCalibrationOptions &options = {});
};

} // namespace epochfrom
