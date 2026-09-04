#pragma once

#include "EquipmentProfile.h"
#include "GaiaCatalog.h"
#include "StarDetector.h"

#include <QString>
#include <QVector>
#include <limits>

namespace epochfrom {

struct DateEstimateOptions {
    StarDetectionOptions detection;

    // Cross-match tolerance for EpochFit's own catalog match, arcsec.
    double maxMatchArcsec = 3.0;

    // Initial epoch guess for the Levenberg-Marquardt fit, Julian years.
    double t0GuessJyear = 2015.0;

    // Assumed per-star astrometric precision fed to EpochFit (weights
    // residuals, and sets the scale of the reported epoch uncertainty) --
    // leave at NaN ("auto") to have estimate() pick a sensible default:
    // equipmentProfile's own rmsAfterHeldoutMas when a profile is given
    // (the actual measured per-star precision after that profile's
    // correction), or EpochFit::fit's own 300mas default otherwise (a
    // deliberately loose number appropriate for an uncorrected plain/
    // low-order-SIP solve). Set explicitly to override either default.
    double obsSigmaMas = std::numeric_limits<double>::quiet_NaN();

    // Optional equipment profile (see EquipmentProfile / EquipmentCalibrator
    // / docs/equipment-profiling-spec.md section 9). When set, each detected
    // star's position is computed from the image's plain LINEAR WCS (CRVAL/
    // CRPIX/CD only, no per-frame SIP) plus this profile's fitted polynomial
    // distortion correction -- not from the platesolver's own per-frame SIP
    // tweak, which the profile is meant to improve on. When null, the
    // platesolver's own WCS (SIP terms included, if it fit any) is used
    // directly instead.
    const EquipmentProfile *equipmentProfile = nullptr;
};

struct DateEstimateResult {
    bool ok = false;
    QString errorMessage;

    QString imagePath;
    QString wcsFilePath;

    int nStarsDetected = 0; // before cross-matching to Gaia
    int nStarsUsed = 0;     // actually used in the epoch fit (post cross-match)
    double medianMatchSepArcsec = std::numeric_limits<double>::quiet_NaN();
    double rmsResidualMas = std::numeric_limits<double>::quiet_NaN();

    double epochJyear = 0.0;
    double epochSigmaYears = std::numeric_limits<double>::quiet_NaN();
    double raOffsetMas = 0.0;
    double decOffsetMas = 0.0;
    bool converged = false;
    bool rankDeficient = false;

    bool usedEquipmentProfile = false;
    double obsSigmaMasUsed = std::numeric_limits<double>::quiet_NaN();

    // Non-empty if an equipment profile was applied and the fitted epoch
    // falls outside that profile's valid_from/valid_to range -- see
    // docs/equipment-profiling-spec.md section 9 ("the equipment may have
    // been adjusted since calibration -- offer to recalibrate").
    QString profileValidityWarning;
};

// Estimates an image's capture epoch: detect stars in the light frame,
// convert each to sky coordinates via the image's WCS (honoring the
// platesolver's own SIP fit, or an EquipmentProfile's calibrated
// distortion correction instead when one is given -- see
// DateEstimateOptions::equipmentProfile), and hand those observed positions
// to EpochFit, which cross-matches against `gaiaCatalog` and fits for the
// epoch at which Gaia's proper-motion-propagated positions best match what
// was actually observed. This is the tool's core dating method, applied to
// one image; see EpochFrom's `date` CLI command for batching over a
// directory.
class ImageDater {
public:
    static DateEstimateResult estimate(const QString &imagePath, const QString &wcsPath,
                                        const QVector<GaiaStar> &gaiaCatalog,
                                        const DateEstimateOptions &options = {});
};

} // namespace epochfrom
