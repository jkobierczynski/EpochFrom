#pragma once

#include "PlateSolver.h"
#include "EquipmentCalibrator.h"
#include "ImageDater.h"

#include <QString>
#include <QTextStream>
#include <QVector>
#include <limits>

namespace epochfrom {

// Shared human-readable report formatting for the three main results this
// tool produces (a plate solve, an equipment calibration, a date estimate).
// Factored out of the CLI (src/cli/main.cpp) so the GUI can print the exact
// same reports into its own log pane instead of re-deriving a second,
// inevitably-drifting copy of this formatting -- see src/gui.

// Simple Julian-year -> approximate calendar date, good enough for a
// human-readable label (the fit result itself is the number that matters).
// 365.25-day Julian year, epoch 2000.0 = 2000-01-01T12:00Z, matching
// astropy's Time(..., format="jyear") convention closely enough for display
// purposes.
QString jyearToDateString(double jyear);

// Prints RA/Dec, field size, pixel scale, image size, and the .wcs path for
// one successful PlateSolveResult. Does not check result.solved -- callers
// are expected to have already handled the failure case (they usually want
// a different message for that, e.g. including the file name in a batch).
void printSolveResult(const PlateSolveResult &result, QTextStream &out);

// Prints the full equipment-calibration report: before/after RMS (combined
// and per-axis), the order-selection table, the internal-repeatability
// diagnostic and its axis-imbalance hint, the limiting-factor verdict, the
// per-sub affine-fit table and its rotation-spread hint, and the per-sub
// residual table with worst-fitting subs called out. `totalSubsInput` is
// the number of subs that were *offered* to the calibrator (for the "Subs
// used: X / Y" line) -- may be larger than result.nSubsUsed if some were
// skipped (failed to solve, no stars, etc).
void printCalibrateResult(const EquipmentCalibrationResult &result, int totalSubsInput,
                           QTextStream &out);

// Prints one image's date estimate: stars detected/used, RMS residual,
// whether an equipment profile was applied, the estimated calendar date,
// the fitted epoch and its uncertainty, the zero-point offset, and any
// rank-deficiency/non-convergence/profile-validity warnings.
void printDateResult(const DateEstimateResult &result, QTextStream &out);

// A single combined epoch estimate for a batch of dated images -- see
// combineDateEstimates().
struct CombinedDateEstimate {
    bool ok = false;
    int nCombined = 0; // how many per-image estimates went into the average
    int nExcluded = 0; // dated images left out (no usable sigma to weight by -- see below)

    double epochJyear = 0.0;
    double epochSigmaYears = std::numeric_limits<double>::quiet_NaN();
};

// Combines a batch's individual per-image epoch estimates into one
// inverse-variance-weighted average (weight = 1 / epochSigmaYears^2), the
// standard way to combine several independent measurements of differing
// precision into a single estimate -- a sub with a tighter epoch
// uncertainty (more/better-matched stars, lower residual RMS) pulls the
// combined date toward itself more than a noisier one, rather than every
// sub counting equally regardless of quality. Only `results` entries with
// result.ok are considered at all; among those, an entry is excluded (and
// counted in nExcluded, not silently dropped) when its fit didn't converge,
// was rank-deficient (its epochSigmaYears is documented as optimistic in
// that case -- see DateEstimateResult -- and a falsely tiny sigma would let
// it dominate the weighted average purely from an unreliable uncertainty,
// not real precision), or has a non-finite/non-positive epochSigmaYears to
// weight by. `ok` is false (with epochJyear/epochSigmaYears left at their
// defaults) if no entry qualified.
CombinedDateEstimate combineDateEstimates(const QVector<DateEstimateResult> &results);

// Prints the combined weighted-average date from combineDateEstimates():
// how many images it's combining (and how many were excluded, if any), the
// resulting calendar date, and the combined epoch +/- its uncertainty.
void printCombinedDateEstimate(const CombinedDateEstimate &combined, QTextStream &out);

} // namespace epochfrom
