#pragma once

#include <QString>
#include <QVector>
#include <limits>

namespace epochfrom {

struct PolyCorrectionMas {
    double dxiMas = 0.0;
    double detaMas = 0.0;
};

// The reusable output of one equipment calibration run: a per-rig optical
// distortion model (a 2D polynomial in pixel offset from CRPIX, fit against
// Gaia -- see docs/equipment-profiling-spec.md), plus the fit-quality
// numbers and provenance a future dating run (or a user deciding whether to
// trust/recalibrate) needs to see. Mirrors the "Equipment Profile" data
// model in that spec (section 8) closely, field for field.
struct EquipmentProfile {
    // User-facing metadata. The calibrator (EquipmentCalibrator) does not
    // fill these in -- they describe the physical setup, which only the
    // caller/user knows -- except where noted.
    QString label;
    double telescopeApertureMm = 0.0;
    double focalLengthMm = 0.0;
    QString correctorType; // "none" | "refractive" | "reflective" | "catadioptric"
    QString cameraModel;
    double pixelSizeUm = 0.0;
    QString calibrationFilter;
    QString validFrom; // ISO date (yyyy-MM-dd), optional
    QString validTo;   // ISO date, optional; empty = still current

    // Fit inputs / provenance -- filled in by the calibrator.
    int nSubsUsed = 0;
    QString referenceCatalogDescription;
    int nStarsMatched = 0;
    double detectionThresholdSigma = 0.0;

    // Fit outputs -- filled in by the calibrator.
    double crpix1 = 0.0;
    double crpix2 = 0.0;
    double pixelScaleNorm = 0.0; // normalization constant the polynomial terms are defined against
    int polyOrderChosen = 0;
    QVector<double> polyCoeffsXi;  // length = (order+1)(order+2)/2, term order matches poly_terms()
    QVector<double> polyCoeffsEta;

    // Fit quality -- filled in by the calibrator. Both in-sample and
    // held-out numbers are always populated together, per the spec's
    // explicit requirement that a user never be shown only the
    // more-flattering in-sample figure.
    double rmsBeforeMas = 0.0;
    double rmsAfterInSampleMas = 0.0;
    double rmsAfterHeldoutMas = 0.0;
    double rmsAfterHeldoutStdMas = 0.0;
    double internalRepeatabilityMedianMas = std::numeric_limits<double>::quiet_NaN();
    double internalRepeatabilityRmsMas = std::numeric_limits<double>::quiet_NaN();

    // Guidance flags.
    bool chromaticCorrectorWarningShown = false;
    // "measurement_precision" | "unclear" -- see EquipmentCalibrator for
    // what this tool can and can't automatically tell apart; deliberately
    // narrower than the spec's illustrative enum (which also lists
    // catalog_depth/detection_noise as possibilities a user can
    // investigate manually, per docs/equipment-profiling-spec.md section 6
    // -- this implementation doesn't claim to auto-diagnose those two).
    QString limitingFactor;

    // Evaluates the fitted polynomial at a pixel offset (already relative
    // to crpix1/crpix2, i.e. pass pixX - crpix1, NOT a raw pixel
    // coordinate) -- returns the (dxi, deta) tangent-plane correction in
    // mas that a linear-WCS-predicted sky position should be adjusted by.
    PolyCorrectionMas evaluateCorrectionMas(double uPx, double vPx) const;

    static bool saveToFile(const EquipmentProfile &profile, const QString &path,
                            QString *errorMessage = nullptr);
    static bool loadFromFile(const QString &path, EquipmentProfile *outProfile,
                              QString *errorMessage = nullptr);
};

} // namespace epochfrom
