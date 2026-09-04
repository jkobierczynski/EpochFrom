// Integration test for the equipment-profiling pipeline (StarDetector +
// LinearWcs + EquipmentCalibrator end to end). Builds a handful of
// synthetic light-frame FITS files + matching .wcs sidecars with stars
// placed on a fixed pixel grid, injects a KNOWN order-2 distortion
// polynomial into where those grid stars sit relative to a synthetic Gaia
// catalog (not into the image itself -- see the comment below for why that
// correctly models the physical picture), and checks that:
//   - the fit recovers that injected distortion (evaluateCorrectionMas at
//     held-out test points matches the injected truth within tolerance),
//   - the before/after RMS numbers make sense (after << before),
//   - order selection lands on an order that actually captures an order-2
//     distortion (not order 1),
//   - the internal repeatability diagnostic runs and returns a sane number.
//
// This doesn't depend on solve-field, real Gaia data, or real captures --
// deterministic and self-contained, like the project's other tests.

#include "EquipmentCalibrator.h"
#include "LinearWcs.h"
#include "SipPolynomial.h"

#include <QCoreApplication>
#include <QFile>
#include <QPair>
#include <QTemporaryDir>
#include <QTextStream>

#include <fitsio.h>

#include <cmath>
#include <random>

using namespace epochfrom;

namespace {

// rotationDeg: position-angle offset applied to the CD matrix on top of the
// plain scaleArcsecPerPix/no-rotation baseline -- lets a caller simulate
// "this sub's own plate solve came out rotated slightly from the truth"
// (the per-sub-affine test scenario below) without touching where the star
// pixels themselves are rendered, which is exactly the physical picture: a
// solve's position-angle error doesn't move any stars, it just mis-reads
// where they are.
bool writeWcsSidecar(const QString &path, double crval1, double crval2, double crpix1,
                      double crpix2, double cdArcsecPerPix, int imageW, int imageH,
                      double rotationDeg = 0.0)
{
    QFile::remove(path);
    int status = 0;
    fitsfile *fptr = nullptr;
    const QString createPath = "!" + path;
    if (fits_create_file(&fptr, createPath.toLocal8Bit().constData(), &status) != 0)
        return false;
    long naxes[1] = {0};
    fits_create_img(fptr, SHORT_IMG, 0, naxes, &status);
    long lval = imageW;
    fits_write_key(fptr, TLONG, "IMAGEW", &lval, "", &status);
    lval = imageH;
    fits_write_key(fptr, TLONG, "IMAGEH", &lval, "", &status);
    char ctype1[] = "RA---TAN", ctype2[] = "DEC--TAN", cunit[] = "deg";
    fits_write_key(fptr, TSTRING, "CTYPE1", ctype1, "", &status);
    fits_write_key(fptr, TSTRING, "CTYPE2", ctype2, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL1", &crval1, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL2", &crval2, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX1", &crpix1, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX2", &crpix2, "", &status);
    const double cdDeg = cdArcsecPerPix / 3600.0;
    const double baseCd11 = -cdDeg, baseCd12 = 0.0, baseCd21 = 0.0, baseCd22 = cdDeg;
    const double th = rotationDeg * M_PI / 180.0;
    const double cosTh = std::cos(th), sinTh = std::sin(th);
    double cd11 = cosTh * baseCd11 - sinTh * baseCd21;
    double cd12 = cosTh * baseCd12 - sinTh * baseCd22;
    double cd21 = sinTh * baseCd11 + cosTh * baseCd21;
    double cd22 = sinTh * baseCd12 + cosTh * baseCd22;
    fits_write_key(fptr, TDOUBLE, "CD1_1", &cd11, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD1_2", &cd12, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD2_1", &cd21, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD2_2", &cd22, "", &status);
    fits_write_key(fptr, TSTRING, "CUNIT1", cunit, "", &status);
    fits_write_key(fptr, TSTRING, "CUNIT2", cunit, "", &status);
    fits_close_file(fptr, &status);
    return status == 0;
}

bool writeLightFrame(const QString &path, int width, int height, const QVector<float> &pixels,
                      const QString &dateObs)
{
    QFile::remove(path);
    int status = 0;
    fitsfile *fptr = nullptr;
    const QString createPath = "!" + path;
    if (fits_create_file(&fptr, createPath.toLocal8Bit().constData(), &status) != 0)
        return false;
    long naxes[2] = {width, height};
    fits_create_img(fptr, FLOAT_IMG, 2, naxes, &status);
    fits_write_img(fptr, TFLOAT, 1, width * height, const_cast<float *>(pixels.constData()), &status);
    QByteArray dateObsBytes = dateObs.toLocal8Bit();
    fits_write_key(fptr, TSTRING, "DATE-OBS", dateObsBytes.data(), "", &status);
    fits_close_file(fptr, &status);
    return status == 0;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        out << "FAIL: could not create temp dir\n";
        return 1;
    }

    // --- Reference (linear, undistorted) WCS ---
    const int width = 400, height = 300;
    const double crval1 = 200.0, crval2 = -10.0;
    const double crpix1 = (width + 1.0) / 2.0, crpix2 = (height + 1.0) / 2.0;
    const double scaleArcsecPerPix = 1.0;
    LinearWcs refWcs(crval1, crval2, crpix1, crpix2, -scaleArcsecPerPix / 3600.0, 0.0, 0.0,
                      scaleArcsecPerPix / 3600.0);
    if (!refWcs.isValid()) {
        out << "FAIL: reference LinearWcs invalid\n";
        return 1;
    }

    // --- Injected order-2 distortion, in mas, terms ordered per
    // SipPolynomial.h (p outer 0..2, q inner 0..2-p): [1, v, v^2, u, uv, u^2] ---
    const double pixelScaleNorm = 200.0; // deliberately smaller than the real project's 2500 -- this is a small synthetic sensor
    const QVector<double> trueCoeffsXi = {40.0, 90.0, 70.0, 180.0, -120.0, 260.0};
    const QVector<double> trueCoeffsEta = {-25.0, -150.0, -55.0, 110.0, 160.0, -80.0};

    auto injectedCorrection = [&](double uPx, double vPx) {
        QVector<double> terms;
        sipPolyTermsInto(uPx / pixelScaleNorm, vPx / pixelScaleNorm, 2, terms);
        double dxi = 0.0, deta = 0.0;
        for (int i = 0; i < terms.size(); ++i) {
            dxi += terms[i] * trueCoeffsXi[i];
            deta += terms[i] * trueCoeffsEta[i];
        }
        return QPair<double, double>(dxi, deta);
    };

    // --- Star grid: fixed pixel positions shared by every sub (no
    // dithering -- not needed for this test) ---
    QVector<QPair<double, double>> gridStars; // (u, v), FITS 1-indexed
    for (int gy = 0; gy < 8; ++gy) {
        for (int gx = 0; gx < 10; ++gx) {
            const double u = 30.0 + gx * 34.0;
            const double v = 25.0 + gy * 34.0;
            if (u > width - 20 || v > height - 20)
                continue;
            gridStars.push_back({u, v});
        }
    }
    out << "Star grid: " << gridStars.size() << " stars\n";

    // --- Synthetic Gaia catalog: one entry per grid star, positioned at
    // the LINEAR WCS prediction PLUS the injected distortion. This is the
    // physically correct way to inject a known distortion for this test:
    // real distortion means a star's TRUE sky position (what Gaia reports)
    // differs from what a naive linear TAN WCS predicts for the pixel it
    // actually lands on -- so the "truth" catalog position is offset from
    // the linear prediction, while the detected pixel position stays
    // exactly on the grid. Zero proper motion, since this test isn't
    // exercising epoch propagation.
    QVector<GaiaStar> catalog;
    for (int i = 0; i < gridStars.size(); ++i) {
        const auto [u, v] = gridStars[i];
        double raLin = 0.0, decLin = 0.0;
        if (!refWcs.pixToWorld(u, v, &raLin, &decLin)) {
            out << "FAIL: reference WCS pixToWorld failed while building the synthetic catalog\n";
            return 1;
        }
        const auto [dxiMas, detaMas] = injectedCorrection(u - crpix1, v - crpix2);
        const double cosDec = std::cos(decLin * M_PI / 180.0);

        GaiaStar star;
        star.sourceId = 1000 + i;
        star.refEpochJyear = 2016.0;
        star.raDeg = raLin + (dxiMas / 3.6e6) / cosDec;
        star.decDeg = decLin + detaMas / 3.6e6;
        star.pmraMasYr = 0.0;
        star.pmdecMasYr = 0.0;
        star.hasParallax = false;
        star.hasRadialVelocity = false;
        star.photGMeanMag = 12.0;
        star.ruwe = 1.0;
        catalog.push_back(star);
    }

    // --- Render nSubs light frames, same star grid, independent noise ---
    const int nSubs = 4;
    const double fwhmPx = 4.0;
    const double sigmaPx = fwhmPx / 2.3548200450309493;
    const double amplitude = 6000.0;
    const double backgroundLevel = 1000.0;
    const double noiseSigma = 12.0;

    QVector<CalibrationSub> subs;
    for (int s = 0; s < nSubs; ++s) {
        QVector<float> pixels(width * height);
        std::mt19937 rng(1000 + s);
        std::normal_distribution<double> noise(0.0, noiseSigma);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                double v = backgroundLevel + noise(rng);
                pixels[y * width + x] = float(v);
            }
        }
        for (const auto &[gu, gv] : gridStars) {
            // gu, gv are FITS 1-indexed; pixel array is 0-indexed (x=gu-1).
            const double cx = gu - 1.0, cy = gv - 1.0;
            const int ix0 = std::max(0, int(cx) - 15), ix1 = std::min(width - 1, int(cx) + 15);
            const int iy0 = std::max(0, int(cy) - 15), iy1 = std::min(height - 1, int(cy) + 15);
            for (int y = iy0; y <= iy1; ++y) {
                for (int x = ix0; x <= ix1; ++x) {
                    const double dx = x - cx, dy = y - cy;
                    pixels[y * width + x] +=
                        float(amplitude * std::exp(-0.5 * (dx * dx + dy * dy) / (sigmaPx * sigmaPx)));
                }
            }
        }

        const QString imagePath = tmpDir.filePath(QString("sub%1.fits").arg(s));
        const QString wcsPath = tmpDir.filePath(QString("sub%1.wcs").arg(s));
        const QString dateObs = QString("2021-0%1-15T04:00:00").arg(s + 1);
        if (!writeLightFrame(imagePath, width, height, pixels, dateObs) ||
            !writeWcsSidecar(wcsPath, crval1, crval2, crpix1, crpix2, scaleArcsecPerPix, width,
                              height)) {
            out << "FAIL: could not write synthetic sub " << s << "\n";
            return 1;
        }
        subs.push_back({imagePath, wcsPath});
    }

    // --- Run the calibrator ---
    EquipmentCalibrationOptions options;
    options.detection.fwhmPx = fwhmPx;
    options.detection.thresholdSigma = 6.0;
    options.candidateOrders = {1, 2, 3};
    options.crossValidationSplits = 10;
    options.pixelScaleNorm = pixelScaleNorm;
    options.minObservationsPerParameter = 2.0; // this synthetic field is small; relax the guard a bit
    // This scenario injects the identical distortion into every sub (no
    // per-sub variation at all -- see the per-sub-affine scenario further
    // down for that), so it's specifically testing whether the shared
    // polynomial alone recovers a known truth; per-sub affine correction
    // would just be a redundant no-op here and isn't what's under test.
    options.fitPerSubAffine = false;

    const EquipmentCalibrationResult result = EquipmentCalibrator::calibrate(subs, catalog, options);

    bool ok = true;
    if (!result.ok) {
        out << "FAIL: calibrate() failed: " << result.errorMessage << "\n";
        return 1;
    }

    out << QString("subs used: %1, pooled observations: %2\n")
               .arg(result.nSubsUsed)
               .arg(result.nStarObservations);
    out << QString("RMS before: %1 mas\n").arg(result.rmsBeforeMas, 0, 'f', 1);
    for (const OrderCandidateResult &c : result.orderCandidates) {
        out << QString("  order %1: in-sample=%2 mas, held-out=%3 +/- %4 mas\n")
                   .arg(c.order)
                   .arg(c.inSampleRmsMas, 0, 'f', 1)
                   .arg(c.heldoutRmsMeanMas, 0, 'f', 1)
                   .arg(c.heldoutRmsStdMas, 0, 'f', 1);
    }
    out << QString("chosen order: %1\n").arg(result.chosenOrder);
    out << QString("internal repeatability: median=%1 mas, rms=%2 mas (n=%3)\n")
               .arg(result.profile.internalRepeatabilityMedianMas, 0, 'f', 1)
               .arg(result.profile.internalRepeatabilityRmsMas, 0, 'f', 1)
               .arg(result.nStarsForRepeatability);
    out << QString("limiting factor: %1\n").arg(result.profile.limitingFactor);

    // The injected distortion has amplitude of order a few hundred mas at
    // the field edges -- "before" RMS should reflect that scale.
    if (!(result.rmsBeforeMas > 100.0)) {
        out << "FAIL: rmsBeforeMas implausibly small for the injected distortion amplitude\n";
        ok = false;
    }
    // An order-1 (purely linear) polynomial cannot represent an order-2
    // distortion -- order selection should not stop at order 1.
    if (result.chosenOrder < 2) {
        out << "FAIL: order selection chose order < 2 for a genuinely order-2 distortion\n";
        ok = false;
    }
    // After fitting, residuals should be small -- close to the centroiding
    // noise floor, nowhere near the pre-fit distortion amplitude.
    if (!(result.profile.rmsAfterHeldoutMas < 80.0)) {
        out << "FAIL: held-out RMS after fitting is too large -- fit did not recover the "
               "injected distortion\n";
        ok = false;
    }

    // Evaluate the fitted profile at held-out points (off the training
    // grid) and compare against the known injected truth directly -- the
    // real test of whether the polynomial that got saved actually matches
    // reality, not just an aggregate RMS number.
    const QVector<QPair<double, double>> testPoints = {
        {60.0, 70.0}, {300.0, 200.0}, {150.0, 250.0}, {350.0, 60.0},
    };
    for (const auto &[u, v] : testPoints) {
        const auto [expectedDxi, expectedDeta] = injectedCorrection(u - crpix1, v - crpix2);
        const PolyCorrectionMas fitted = result.profile.evaluateCorrectionMas(u - crpix1, v - crpix2);
        const double err = std::hypot(fitted.dxiMas - expectedDxi, fitted.detaMas - expectedDeta);
        out << QString("  test point (%1,%2): injected=(%3,%4) fitted=(%5,%6) err=%7 mas\n")
                   .arg(u)
                   .arg(v)
                   .arg(expectedDxi, 0, 'f', 1)
                   .arg(expectedDeta, 0, 'f', 1)
                   .arg(fitted.dxiMas, 0, 'f', 1)
                   .arg(fitted.detaMas, 0, 'f', 1)
                   .arg(err, 0, 'f', 1);
        if (!(err < 100.0)) {
            out << "  FAIL: fitted correction too far from the injected truth at this point\n";
            ok = false;
        }
    }

    if (!std::isfinite(result.profile.internalRepeatabilityRmsMas)) {
        out << "FAIL: internal repeatability diagnostic did not produce a number\n";
        ok = false;
    }

    // --- Axis-split (xi/eta) RMS ---
    out << QString("RMS before by axis: xi=%1 mas, eta=%2 mas (combined=%3 mas)\n")
               .arg(result.rmsBeforeXiMas, 0, 'f', 1)
               .arg(result.rmsBeforeEtaMas, 0, 'f', 1)
               .arg(result.rmsBeforeMas, 0, 'f', 1);
    out << QString("RMS after by axis:  xi=%1 mas, eta=%2 mas (combined=%3 mas)\n")
               .arg(result.rmsAfterXiMas, 0, 'f', 1)
               .arg(result.rmsAfterEtaMas, 0, 'f', 1)
               .arg(result.profile.rmsAfterInSampleMas, 0, 'f', 1);
    auto checkQuadrature = [&](const char *label, double xi, double eta, double combined) {
        if (!std::isfinite(xi) || !std::isfinite(eta) || !(xi > 0.0) || !(eta > 0.0)) {
            out << QString("FAIL: %1 xi/eta RMS not sane positive numbers\n").arg(label);
            ok = false;
            return;
        }
        const double recombined = std::hypot(xi, eta);
        if (std::abs(recombined - combined) > 0.05 * std::max(combined, 1.0)) {
            out << QString("FAIL: %1 xi/eta RMS (%2, %3 -> hypot %4) doesn't reconstruct the "
                            "combined RMS (%5)\n")
                       .arg(label)
                       .arg(xi, 0, 'f', 2)
                       .arg(eta, 0, 'f', 2)
                       .arg(recombined, 0, 'f', 2)
                       .arg(combined, 0, 'f', 2);
            ok = false;
        }
    };
    checkQuadrature("before", result.rmsBeforeXiMas, result.rmsBeforeEtaMas, result.rmsBeforeMas);
    checkQuadrature("after", result.rmsAfterXiMas, result.rmsAfterEtaMas, result.profile.rmsAfterInSampleMas);
    if (!std::isfinite(result.internalRepeatabilityXiMas) || !std::isfinite(result.internalRepeatabilityEtaMas)) {
        out << "FAIL: internal repeatability xi/eta split did not produce numbers\n";
        ok = false;
    }

    // --- Per-observation export (ResidualObservation) ---
    if (result.observations.size() != result.nStarObservations) {
        out << QString("FAIL: observations has %1 entries, expected nStarObservations (%2)\n")
                   .arg(result.observations.size())
                   .arg(result.nStarObservations);
        ok = false;
    }
    int nKeptTotal = 0;
    for (const ResidualObservation &ro : result.observations) {
        const double expectedRadius = std::hypot(ro.pixelUOffset, ro.pixelVOffset);
        if (std::abs(expectedRadius - ro.radiusPx) > 1e-6) {
            out << "FAIL: ResidualObservation.radiusPx doesn't match hypot(u, v)\n";
            ok = false;
        }
        if (ro.subIndex < 0 || ro.subIndex >= subs.size()) {
            out << "FAIL: ResidualObservation.subIndex out of range\n";
            ok = false;
        }
        if (ro.keptInFit)
            ++nKeptTotal;
    }
    int nKeptFromSubs = 0;
    for (const SubResidualResult &s : result.subResiduals)
        nKeptFromSubs += s.nKept;
    if (nKeptTotal != nKeptFromSubs) {
        out << QString("FAIL: observations kept count (%1) doesn't match subResiduals' total nKept "
                        "(%2)\n")
                   .arg(nKeptTotal)
                   .arg(nKeptFromSubs);
        ok = false;
    }

    // --- Per-sub residual report ---
    if (result.subResiduals.size() != subs.size()) {
        out << QString("FAIL: subResiduals has %1 entries, expected one per sub (%2)\n")
                   .arg(result.subResiduals.size())
                   .arg(subs.size());
        ok = false;
    }
    for (const SubResidualResult &s : result.subResiduals) {
        out << QString("  sub %1: obs=%2 kept=%3 rmsBefore=%4 mas rmsAfter=%5 mas%6\n")
                   .arg(s.subIndex)
                   .arg(s.nObservations)
                   .arg(s.nKept)
                   .arg(s.rmsBeforeMas, 0, 'f', 1)
                   .arg(s.rmsAfterMas, 0, 'f', 1)
                   .arg(s.skipReason.isEmpty() ? QString() : QString(" (skipped: %1)").arg(s.skipReason));
        // Every sub in this test is fully usable -- none should be flagged
        // as skipped, and each should have contributed observations from
        // the whole star grid.
        if (!s.skipReason.isEmpty()) {
            out << QString("FAIL: sub %1 unexpectedly skipped: %2\n").arg(s.subIndex).arg(s.skipReason);
            ok = false;
        }
        if (s.nObservations != gridStars.size()) {
            out << QString("FAIL: sub %1 has %2 observations, expected %3 (every grid star)\n")
                       .arg(s.subIndex)
                       .arg(s.nObservations)
                       .arg(gridStars.size());
            ok = false;
        }
        if (s.nKept < 0 || s.nKept > s.nObservations) {
            out << QString("FAIL: sub %1 nKept (%2) out of range for nObservations (%3)\n")
                       .arg(s.subIndex)
                       .arg(s.nKept)
                       .arg(s.nObservations);
            ok = false;
        }
        if (!std::isfinite(s.rmsBeforeMas) || !(s.rmsBeforeMas > 0.0)) {
            out << QString("FAIL: sub %1 rmsBeforeMas not a sane positive number\n").arg(s.subIndex);
            ok = false;
        }
        if (!std::isfinite(s.rmsAfterMas) || !(s.rmsAfterMas < 80.0)) {
            out << QString("FAIL: sub %1 rmsAfterMas not small after fitting the injected distortion\n")
                       .arg(s.subIndex);
            ok = false;
        }
    }

    // --- Per-sub affine correction (fitPerSubAffine) ---
    // Same shared order-2 optical distortion and the same rendered star
    // images as above (a position-angle error doesn't move any stars, it
    // just changes how a WCS reads their position), but now each sub's own
    // .wcs sidecar is deliberately solved with a DIFFERENT small rotation
    // error on top of the truth -- simulating four independent plate solves
    // that each came out slightly off, the way real ones do. This is
    // exactly the scenario fitPerSubAffine exists for: a single shared
    // polynomial can't represent an error that differs sub to sub, so
    // pooling raw residuals should leave a real gap between the per-sub and
    // shared-only interpretation of what's happening.
    out << "\n--- Per-sub affine correction ---\n";
    const QVector<double> rotOffsetsDeg = {0.06, -0.09, 0.12, -0.04};
    QVector<CalibrationSub> rotatedSubs;
    for (int s = 0; s < nSubs; ++s) {
        const QString wcsPath = tmpDir.filePath(QString("sub%1_rot.wcs").arg(s));
        if (!writeWcsSidecar(wcsPath, crval1, crval2, crpix1, crpix2, scaleArcsecPerPix, width,
                              height, rotOffsetsDeg[s])) {
            out << "FAIL: could not write rotated wcs sidecar for sub " << s << "\n";
            ok = false;
        }
        // Reuses subs[s].imagePath -- the star pixel positions are
        // unaffected by which WCS is used to read them.
        rotatedSubs.push_back({subs[s].imagePath, wcsPath});
    }

    EquipmentCalibrationOptions rotOptionsOn = options;
    rotOptionsOn.fitPerSubAffine = true;
    const EquipmentCalibrationResult rotResultOn =
        EquipmentCalibrator::calibrate(rotatedSubs, catalog, rotOptionsOn);
    EquipmentCalibrationOptions rotOptionsOff = options;
    rotOptionsOff.fitPerSubAffine = false;
    const EquipmentCalibrationResult rotResultOff =
        EquipmentCalibrator::calibrate(rotatedSubs, catalog, rotOptionsOff);

    if (!rotResultOn.ok || !rotResultOff.ok) {
        out << "FAIL: calibrate() failed on the per-sub-rotation scenario\n";
        ok = false;
    } else {
        out << QString("  fitPerSubAffine=true:  after held-out RMS %1 mas, chosen order %2\n")
                   .arg(rotResultOn.profile.rmsAfterHeldoutMas, 0, 'f', 1)
                   .arg(rotResultOn.chosenOrder);
        out << QString("  fitPerSubAffine=false: after held-out RMS %1 mas, chosen order %2\n")
                   .arg(rotResultOff.profile.rmsAfterHeldoutMas, 0, 'f', 1)
                   .arg(rotResultOff.chosenOrder);

        // The whole point: separating each sub's own rotation error first
        // should let the shared fit recover the real (order-2) optical
        // distortion about as well as scenario 1 did, despite the added
        // per-sub noise.
        if (!(rotResultOn.profile.rmsAfterHeldoutMas < 80.0)) {
            out << "FAIL: fitPerSubAffine=true did not recover a small held-out RMS despite "
                   "per-sub rotation noise\n";
            ok = false;
        }
        if (rotResultOn.chosenOrder < 2) {
            out << "FAIL: fitPerSubAffine=true chose order < 2 -- per-sub correction destroyed "
                   "the shared order-2 signal instead of just removing per-sub noise\n";
            ok = false;
        }
        // And the comparison that justifies the feature: pooling the same
        // per-sub-rotated data WITHOUT separating it out first should come
        // out clearly worse, since one shared polynomial can't represent
        // four different rotation errors at once.
        if (!(rotResultOff.profile.rmsAfterHeldoutMas > rotResultOn.profile.rmsAfterHeldoutMas + 20.0)) {
            out << "FAIL: fitPerSubAffine=false wasn't meaningfully worse than fitPerSubAffine=true "
                   "on data with real per-sub rotation differences -- expected the per-sub step to "
                   "matter here\n";
            ok = false;
        }

        if (rotResultOn.subAffineFits.size() != rotatedSubs.size()) {
            out << QString("FAIL: subAffineFits has %1 entries, expected one per sub (%2)\n")
                       .arg(rotResultOn.subAffineFits.size())
                       .arg(rotatedSubs.size());
            ok = false;
        }
        for (const SubAffineFit &a : rotResultOn.subAffineFits) {
            out << QString("  sub %1: rotation=%2 deg, scale=%3%%, rmsBefore=%4 mas, rmsAfter=%5 mas\n")
                       .arg(a.subIndex)
                       .arg(a.rotationDeg, 0, 'f', 4)
                       .arg(a.scaleErrorPct, 0, 'f', 3)
                       .arg(a.rmsBeforeMas, 0, 'f', 1)
                       .arg(a.rmsAfterMas, 0, 'f', 1);
            if (!a.fitted) {
                out << QString("FAIL: sub %1's own affine fit did not run (skipped: %2)\n")
                           .arg(a.subIndex)
                           .arg(a.skipReason);
                ok = false;
                continue;
            }
            if (!(a.rmsAfterMas < a.rmsBeforeMas)) {
                out << QString("FAIL: sub %1's own affine fit didn't improve its RMS\n").arg(a.subIndex);
                ok = false;
            }
        }

        // The fitted per-sub rotations should track the injected ones
        // (proportionally -- sign/scale convention isn't pinned down here,
        // just that a bigger injected rotation error produces a bigger
        // fitted one, and different subs are told apart from each other,
        // not collapsed to one shared value). A strong Pearson correlation
        // is a sign-agnostic way to check that without a fragile
        // point-by-point tolerance on a derived diagnostic number.
        if (rotResultOn.subAffineFits.size() == rotOffsetsDeg.size()) {
            double meanInj = 0.0, meanFit = 0.0;
            for (int i = 0; i < rotOffsetsDeg.size(); ++i) {
                meanInj += rotOffsetsDeg[i];
                meanFit += rotResultOn.subAffineFits[i].rotationDeg;
            }
            meanInj /= rotOffsetsDeg.size();
            meanFit /= rotOffsetsDeg.size();
            double cov = 0.0, varInj = 0.0, varFit = 0.0;
            for (int i = 0; i < rotOffsetsDeg.size(); ++i) {
                const double di = rotOffsetsDeg[i] - meanInj;
                const double df = rotResultOn.subAffineFits[i].rotationDeg - meanFit;
                cov += di * df;
                varInj += di * di;
                varFit += df * df;
            }
            const double corr = (varInj > 0.0 && varFit > 0.0) ? cov / std::sqrt(varInj * varFit)
                                                                 : std::numeric_limits<double>::quiet_NaN();
            out << QString("  injected-vs-fitted rotation correlation: %1\n").arg(corr, 0, 'f', 3);
            if (!(std::abs(corr) > 0.9)) {
                out << "FAIL: fitted per-sub rotations don't track the injected per-sub rotations "
                       "(expected |correlation| > 0.9)\n";
                ok = false;
            }
        }
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
