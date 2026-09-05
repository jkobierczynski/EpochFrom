#include "ReportFormatting.h"

#include <QDate>
#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace epochfrom {

QString jyearToDateString(double jyear)
{
    // -0.5: J2000.0 is noon, not midnight.
    const double daysSinceJ2000 = (jyear - 2000.0) * 365.25 - 0.5;
    const QDate j2000(2000, 1, 1);
    return j2000.addDays(static_cast<qint64>(std::lround(daysSinceJ2000))).toString("yyyy-MM-dd");
}

void printSolveResult(const PlateSolveResult &result, QTextStream &out)
{
    out << "RA:            " << QString::number(result.centerRaDeg, 'f', 6) << " deg\n";
    out << "Dec:           " << QString::number(result.centerDecDeg, 'f', 6) << " deg\n";
    out << "Field size:    " << QString::number(result.fieldWidthArcmin, 'f', 2) << " x "
        << QString::number(result.fieldHeightArcmin, 'f', 2) << " arcmin\n";
    out << "Pixel scale:   " << QString::number(result.pixelScaleArcsecPerPix, 'f', 3)
        << " arcsec/px\n";
    out << "Image size:    " << result.imageWidthPx << " x " << result.imageHeightPx << " px\n";
    out << "WCS file:      " << result.wcsFilePath << "\n";
}

void printCalibrateResult(const EquipmentCalibrationResult &result, int totalSubsInput,
                           QTextStream &out)
{
    out << "\n--- Equipment calibration ---\n";
    out << "Subs used:            " << result.nSubsUsed << " / " << totalSubsInput << "\n";
    out << "Pooled observations:  " << result.nStarObservations << "\n";
    out << "RMS before (linear WCS only): " << QString::number(result.rmsBeforeMas, 'f', 1) << " mas"
        << QString(" (RA/xi axis %1 mas, Dec/eta axis %2 mas)")
               .arg(result.rmsBeforeXiMas, 0, 'f', 1)
               .arg(result.rmsBeforeEtaMas, 0, 'f', 1)
        << "\n";
    // subAffineFits is only populated when per-sub affine fitting was on
    // (one entry per input sub -- see its struct comment), so its emptiness
    // doubles as the options.fitPerSubAffine flag this report needs, without
    // having to thread the calibration options themselves through here too.
    if (!result.subAffineFits.isEmpty()) {
        out << "RMS after per-sub rotation/scale/shear (before the shared fit): "
            << QString::number(result.rmsAfterSubAffineMas, 'f', 1) << " mas"
            << QString(" (RA/xi axis %1 mas, Dec/eta axis %2 mas)")
                   .arg(result.rmsAfterSubAffineXiMas, 0, 'f', 1)
                   .arg(result.rmsAfterSubAffineEtaMas, 0, 'f', 1)
            << "\n";
    }
    out << "\n";
    out << QString("%1  %2  %3  %4\n")
               .arg("order", -6)
               .arg("in-sample RMS", -16)
               .arg("held-out RMS (mean +/- std)", -30)
               .arg("kept");
    for (const OrderCandidateResult &c : result.orderCandidates) {
        out << QString("%1  %2 mas      %3 +/- %4 mas          %5\n")
                   .arg(c.order, -6)
                   .arg(c.inSampleRmsMas, 8, 'f', 1)
                   .arg(c.heldoutRmsMeanMas, 8, 'f', 1)
                   .arg(c.heldoutRmsStdMas, -6, 'f', 1)
                   .arg(c.nKeptInSample);
    }
    out << "\nChosen order: " << result.chosenOrder << "\n";
    if (result.overfittingWarning)
        out << "NOTE: " << result.overfittingWarningText << "\n";

    out << QString("\nAfter fitting:        in-sample RMS %1 mas, held-out RMS %2 +/- %3 mas\n")
               .arg(result.profile.rmsAfterInSampleMas, 0, 'f', 1)
               .arg(result.profile.rmsAfterHeldoutMas, 0, 'f', 1)
               .arg(result.profile.rmsAfterHeldoutStdMas, 0, 'f', 1);
    out << QString("                       (kept-observation RMS by axis: RA/xi %1 mas, Dec/eta %2 "
                    "mas)\n")
               .arg(result.rmsAfterXiMas, 0, 'f', 1)
               .arg(result.rmsAfterEtaMas, 0, 'f', 1);

    if (result.nStarsForRepeatability > 0) {
        out << QString("Internal repeatability (sub-to-sub, %1 stars, no Gaia involved): median %2 "
                        "mas, RMS %3 mas")
                   .arg(result.nStarsForRepeatability)
                   .arg(result.profile.internalRepeatabilityMedianMas, 0, 'f', 1)
                   .arg(result.profile.internalRepeatabilityRmsMas, 0, 'f', 1);
        out << QString(" (by axis: RA/xi %1 mas, Dec/eta %2 mas)\n")
                   .arg(result.internalRepeatabilityXiMas, 0, 'f', 1)
                   .arg(result.internalRepeatabilityEtaMas, 0, 'f', 1);
    } else {
        out << "Internal repeatability: not enough per-star sub coverage to compute (need more "
               "subs, or subs with more overlap)\n";
    }

    // A pronounced imbalance between the two axes' repeatability (not the
    // before/after distortion fit, which mixes in the optics) points at a
    // cause tied to one mount axis specifically -- periodic error or poor
    // polar alignment, both of which act along RA or Dec respectively, not
    // symmetrically across the field the way field curvature would. Report
    // it as a hint, not a verdict -- this doesn't rule out other causes.
    if (result.nStarsForRepeatability > 0 && std::isfinite(result.internalRepeatabilityXiMas) &&
        std::isfinite(result.internalRepeatabilityEtaMas)) {
        const double lo = std::min(result.internalRepeatabilityXiMas, result.internalRepeatabilityEtaMas);
        const double hi = std::max(result.internalRepeatabilityXiMas, result.internalRepeatabilityEtaMas);
        if (lo > 0.0 && hi / lo > 1.5) {
            const bool xiWorse = result.internalRepeatabilityXiMas > result.internalRepeatabilityEtaMas;
            out << QString("-> Sub-to-sub repeatability is noticeably worse in %1 (%2 mas) than %3 "
                            "(%4 mas) -- %5 mas is a real difference, not just noise between two "
                            "similar numbers, and points at something tied to that mount axis "
                            "specifically (tracking/periodic error for RA, polar alignment/drift "
                            "for Dec) rather than the optics, which would affect both axes more "
                            "evenly.\n")
                       .arg(xiWorse ? "RA/xi" : "Dec/eta")
                       .arg(hi, 0, 'f', 1)
                       .arg(xiWorse ? "Dec/eta" : "RA/xi")
                       .arg(lo, 0, 'f', 1)
                       .arg(hi - lo, 0, 'f', 1);
        }
    }

    if (result.profile.limitingFactor == "measurement_precision") {
        out << "-> Your subs agree with each other about as well as they agree with Gaia after "
               "calibration: measurement/centroiding precision looks like the limiting factor "
               "here, not the fit. A better polynomial order or more reference stars is unlikely "
               "to help further; better data (sharper focus, more subs, narrower filter) would.\n";
    } else {
        out << "-> There's a real gap between how well your subs agree with each other and how "
               "well they match Gaia after calibration -- that could be a genuine remaining "
               "distortion, or a data/matching issue worth a closer look, rather than just "
               "measurement noise.\n";
    }

    if (!result.subAffineFits.isEmpty()) {
        out << "\n--- Per-sub affine fit (rotation/scale/shear, removed before the shared fit) ---\n";
        out << QString("%1  %2  %3  %4  %5  %6\n")
                   .arg("#", -4)
                   .arg("rotation", -12)
                   .arg("scale err", -12)
                   .arg("shear", -12)
                   .arg("RMS before", -12)
                   .arg("RMS after");
        for (const SubAffineFit &a : result.subAffineFits) {
            if (!a.fitted) {
                out << QString("%1  -- skipped: %2\n").arg(a.subIndex, -4).arg(a.skipReason);
                continue;
            }
            out << QString("%1  %2 deg   %3 %%    %4 deg   %5 mas   %6 mas\n")
                       .arg(a.subIndex, -4)
                       .arg(a.rotationDeg, 8, 'f', 4)
                       .arg(a.scaleErrorPct, 8, 'f', 3)
                       .arg(a.shearDeg, 8, 'f', 4)
                       .arg(a.rmsBeforeMas, 8, 'f', 1)
                       .arg(a.rmsAfterMas, 8, 'f', 1);
        }
        // A big spread here (rather than every sub landing on nearly the
        // same rotation/scale) is the direct evidence for "this session's
        // subs were each solved with their own small, independent error" --
        // the shared distortion polynomial can't explain that kind of
        // spread no matter its order, since it fits one function of pixel
        // position shared by every sub.
        QVector<double> rotations;
        for (const SubAffineFit &a : result.subAffineFits) {
            if (a.fitted && std::isfinite(a.rotationDeg))
                rotations.push_back(a.rotationDeg);
        }
        if (rotations.size() > 1) {
            const double lo = *std::min_element(rotations.begin(), rotations.end());
            const double hi = *std::max_element(rotations.begin(), rotations.end());
            if (hi - lo > 0.02) {
                out << QString("-> Fitted rotation varies by %1 deg across subs (%2 to %3) -- these "
                                "subs' own plate solves didn't agree with each other on position "
                                "angle, which is exactly what a shared spatial distortion fit can't "
                                "correct for on its own.\n")
                           .arg(hi - lo, 0, 'f', 4)
                           .arg(lo, 0, 'f', 4)
                           .arg(hi, 0, 'f', 4);
            }
        }
    }

    if (!result.subResiduals.isEmpty()) {
        out << "\n--- Per-sub residuals (chosen order " << result.chosenOrder << ") ---\n";
        out << QString("%1  %2  %3  %4  %5  %6\n")
                   .arg("#", -4)
                   .arg("file", -28)
                   .arg("obs", -6)
                   .arg("kept", -6)
                   .arg("RMS before", -12)
                   .arg("RMS after");
        for (const SubResidualResult &s : result.subResiduals) {
            const QString baseName = QFileInfo(s.imagePath).fileName();
            if (!s.skipReason.isEmpty()) {
                out << QString("%1  %2  -- skipped: %3\n")
                           .arg(s.subIndex, -4)
                           .arg(baseName.left(28), -28)
                           .arg(s.skipReason);
                continue;
            }
            out << QString("%1  %2  %3  %4  %5 mas   %6 mas\n")
                       .arg(s.subIndex, -4)
                       .arg(baseName.left(28), -28)
                       .arg(s.nObservations, -6)
                       .arg(s.nKept, -6)
                       .arg(s.rmsBeforeMas, 8, 'f', 1)
                       .arg(s.rmsAfterMas, 8, 'f', 1);
        }

        // Flag the worst-fitting subs (by post-fit RMS) so a large aggregate
        // residual can be traced back to specific frames rather than left as
        // one opaque pooled number -- the whole point of this report.
        QVector<SubResidualResult> ranked;
        for (const SubResidualResult &s : result.subResiduals) {
            if (s.skipReason.isEmpty() && std::isfinite(s.rmsAfterMas))
                ranked.push_back(s);
        }
        if (ranked.size() > 3) {
            std::sort(ranked.begin(), ranked.end(), [](const SubResidualResult &a, const SubResidualResult &b) {
                return a.rmsAfterMas > b.rmsAfterMas;
            });
            out << "\nWorst-fitting subs (post-fit RMS):\n";
            const int nWorst = std::min(3, int(ranked.size()));
            for (int i = 0; i < nWorst; ++i) {
                out << QString("  %1  %2 mas\n")
                           .arg(QFileInfo(ranked[i].imagePath).fileName())
                           .arg(ranked[i].rmsAfterMas, 0, 'f', 1);
            }
        }
    }
}

void printDateResult(const DateEstimateResult &result, QTextStream &out)
{
    out << "Image:               " << result.imagePath << "\n";
    out << "Stars detected:      " << result.nStarsDetected << "\n";
    out << "Stars used:          " << result.nStarsUsed << " (median match sep "
        << QString::number(result.medianMatchSepArcsec, 'f', 3) << "\")\n";
    out << "RMS residual:        " << QString::number(result.rmsResidualMas, 'f', 1) << " mas\n";
    out << "Equipment profile:   "
        << (result.usedEquipmentProfile ? "applied" : "none -- using platesolver's own WCS")
        << " (assumed per-star precision " << QString::number(result.obsSigmaMasUsed, 'f', 0)
        << " mas)\n";
    out << "\nEstimated date:      " << jyearToDateString(result.epochJyear) << "\n";
    out << "Epoch:               " << QString::number(result.epochJyear, 'f', 4) << " +/- "
        << QString::number(result.epochSigmaYears, 'f', 4) << " yr ("
        << QString::number(result.epochSigmaYears * 365.25, 'f', 0) << " days)\n";
    out << "Zero-point offset:   RA " << QString::number(result.raOffsetMas, 'f', 1) << " mas, Dec "
        << QString::number(result.decOffsetMas, 'f', 1) << " mas\n";
    if (result.rankDeficient)
        out << "WARNING: fit Jacobian is rank-deficient -- epoch and the RA/Dec offset aren't "
               "fully separable with this star set; the epoch uncertainty above is optimistic.\n";
    if (!result.converged)
        out << "WARNING: fit did not report convergence.\n";
    if (!result.profileValidityWarning.isEmpty())
        out << "WARNING: " << result.profileValidityWarning << "\n";
}

} // namespace epochfrom
