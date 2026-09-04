#include "ImageDater.h"
#include "EpochFit.h"
#include "FitsImage.h"
#include "LinearWcs.h"
#include "PlateSolver.h"
#include "Wcs.h"

#include <QDate>

#include <cmath>

namespace epochfrom {

namespace {

// Same Julian-year -> approximate calendar date convention the CLI's
// jyearToDateString() uses (365.25-day Julian year, epoch 2000.0 =
// 2000-01-01T12:00Z) -- duplicated here rather than shared across modules,
// same call the project already made for FitsImage.cpp's inverse
// conversion. Only used internally, for comparing a fitted epoch against an
// EquipmentProfile's valid_from/valid_to date strings.
QDate jyearToApproxDate(double jyear)
{
    const double daysSinceJ2000 = (jyear - 2000.0) * 365.25 - 0.5;
    const QDate j2000(2000, 1, 1);
    return j2000.addDays(static_cast<qint64>(std::lround(daysSinceJ2000)));
}

} // namespace

DateEstimateResult ImageDater::estimate(const QString &imagePath, const QString &wcsPath,
                                          const QVector<GaiaStar> &gaiaCatalog,
                                          const DateEstimateOptions &options)
{
    DateEstimateResult result;
    result.imagePath = imagePath;
    result.wcsFilePath = wcsPath;

    if (gaiaCatalog.isEmpty()) {
        result.errorMessage = QStringLiteral("empty Gaia catalog");
        return result;
    }

    const FitsImageData image = FitsImage::load(imagePath);
    if (!image.loaded) {
        result.errorMessage = image.errorMessage.isEmpty()
                                   ? QStringLiteral("failed to load FITS image")
                                   : QStringLiteral("failed to load FITS image: %1").arg(image.errorMessage);
        return result;
    }

    const QVector<DetectedStar> detected = StarDetector::detect(image, options.detection);
    result.nStarsDetected = detected.size();
    if (detected.isEmpty()) {
        result.errorMessage = QStringLiteral("no stars detected in the image");
        return result;
    }

    QVector<EpochFit::ObservedStar> observed;
    observed.reserve(detected.size());

    if (options.equipmentProfile) {
        // Plain linear WCS (no per-frame SIP tweak) plus the profile's own
        // calibrated correction -- see docs/equipment-profiling-spec.md
        // section 9. This deliberately does NOT use the platesolver's own
        // SIP fit: the profile is meant to replace it, not stack on top of
        // it (and stacking would double-correct part of the same
        // distortion the solver's own low-order tweak already guessed at).
        const PlateSolveResult wcs = PlateSolver::readWcsFile(wcsPath);
        if (!wcs.solved) {
            result.errorMessage = QStringLiteral("failed to read WCS %1: %2").arg(wcsPath, wcs.errorMessage);
            return result;
        }
        const LinearWcs linearWcs(wcs.crval1Deg, wcs.crval2Deg, wcs.crpix1, wcs.crpix2, wcs.cd11,
                                    wcs.cd12, wcs.cd21, wcs.cd22);
        if (!linearWcs.isValid()) {
            result.errorMessage =
                QStringLiteral("invalid linear WCS: %1").arg(linearWcs.errorMessage());
            return result;
        }

        const EquipmentProfile &profile = *options.equipmentProfile;
        for (const DetectedStar &star : detected) {
            // Detected coordinates are 0-indexed; WCS pixel coordinates are
            // FITS 1-indexed.
            const double pixX = star.x + 1.0;
            const double pixY = star.y + 1.0;
            double raLin = 0.0, decLin = 0.0;
            if (!linearWcs.pixToWorld(pixX, pixY, &raLin, &decLin))
                continue;

            const double uPx = pixX - profile.crpix1;
            const double vPx = pixY - profile.crpix2;
            const PolyCorrectionMas corr = profile.evaluateCorrectionMas(uPx, vPx);
            const double cosDec = std::cos(decLin * M_PI / 180.0);

            EpochFit::ObservedStar obs;
            obs.raDeg = raLin + (corr.dxiMas / 3.6e6) / (cosDec != 0.0 ? cosDec : 1.0);
            obs.decDeg = decLin + corr.detaMas / 3.6e6;
            observed.push_back(obs);
        }
        result.usedEquipmentProfile = true;
    } else {
        // No profile: trust the platesolver's own WCS (SIP terms included,
        // if it fit any) directly.
        const Wcs wcs(wcsPath);
        if (!wcs.isValid()) {
            result.errorMessage = QStringLiteral("failed to read WCS %1: %2").arg(wcsPath, wcs.errorMessage());
            return result;
        }
        for (const DetectedStar &star : detected) {
            double ra = 0.0, dec = 0.0;
            if (!wcs.pixToWorld(star.x + 1.0, star.y + 1.0, &ra, &dec))
                continue;
            EpochFit::ObservedStar obs;
            obs.raDeg = ra;
            obs.decDeg = dec;
            observed.push_back(obs);
        }
    }

    if (observed.isEmpty()) {
        result.errorMessage = QStringLiteral("no detected star could be converted to sky coordinates");
        return result;
    }

    double obsSigmaMas = options.obsSigmaMas;
    if (!std::isfinite(obsSigmaMas)) {
        obsSigmaMas = (options.equipmentProfile && std::isfinite(options.equipmentProfile->rmsAfterHeldoutMas))
                          ? options.equipmentProfile->rmsAfterHeldoutMas
                          : 300.0; // EpochFit::fit's own default, for an uncorrected solve
    }
    result.obsSigmaMasUsed = obsSigmaMas;

    const EpochFit::Result fit = EpochFit::fit(gaiaCatalog, observed, obsSigmaMas,
                                                 options.t0GuessJyear, options.maxMatchArcsec);
    if (fit.nStarsUsed == 0) {
        result.errorMessage =
            QStringLiteral("no detected star cross-matched to the Gaia catalog within %1\"")
                .arg(options.maxMatchArcsec);
        return result;
    }

    result.epochJyear = fit.epochJyear;
    result.epochSigmaYears = fit.epochSigmaYears;
    result.raOffsetMas = fit.raOffsetMas;
    result.decOffsetMas = fit.decOffsetMas;
    result.nStarsUsed = fit.nStarsUsed;
    result.medianMatchSepArcsec = fit.medianMatchSepArcsec;
    result.rmsResidualMas = fit.rmsResidualMas;
    result.converged = fit.converged;
    result.rankDeficient = fit.rankDeficient;

    if (options.equipmentProfile) {
        const EquipmentProfile &profile = *options.equipmentProfile;
        const QDate fittedDate = jyearToApproxDate(result.epochJyear);
        const QDate validFrom = QDate::fromString(profile.validFrom, "yyyy-MM-dd");
        const QDate validTo = QDate::fromString(profile.validTo, "yyyy-MM-dd");
        if (validFrom.isValid() && fittedDate < validFrom) {
            result.profileValidityWarning =
                QStringLiteral("fitted date %1 is before this equipment profile's valid_from (%2) -- "
                                "the rig may have been in a different configuration then; consider a "
                                "profile calibrated nearer that date")
                    .arg(fittedDate.toString("yyyy-MM-dd"), profile.validFrom);
        } else if (validTo.isValid() && fittedDate > validTo) {
            result.profileValidityWarning =
                QStringLiteral("fitted date %1 is after this equipment profile's valid_to (%2) -- the "
                                "equipment may have been adjusted (refocus, spacing) since calibration; "
                                "consider recalibrating")
                    .arg(fittedDate.toString("yyyy-MM-dd"), profile.validTo);
        }
    }

    result.ok = true;
    return result;
}

} // namespace epochfrom
