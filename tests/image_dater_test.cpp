// Integration test for ImageDater: detect synthetic stars in a rendered
// light frame, cross-match against a synthetic Gaia catalog with known
// proper motions, and check the fitted epoch recovers a known "true" epoch
// -- both without an equipment profile (trusting the image's own plain
// linear WCS directly, via the general Wcs class) and with one (a known
// injected distortion, corrected via a synthetic EquipmentProfile, via
// LinearWcs + PolyCorrectionMas), using the SAME rendered image for both:
// what differs between the two cases is which synthetic Gaia catalog is
// used, not the pixel data, since a real rig's actual distortion is a
// property of the optics, not something that changes what pixel a star
// lands on.
//
// Catalog construction deliberately avoids needing an inverse WCS (world ->
// pixel), which this project has no use for elsewhere and so doesn't
// implement: each star's *observed* sky position at the fixed pixel grid is
// computed forward (LinearWcs::pixToWorld, plus the injected correction for
// the "with profile" case), then the catalog's 2016.0 reference position is
// derived by walking that observed position backward at the star's own
// proper motion over (trueEpoch - 2016.0) years -- a flat-tangent-plane
// approximation, but an excellent one at these baselines (a few years) and
// proper motions (tens of mas/yr): SpaceMotion::propagate's own rigorous
// 3D-vector method reduces to the same thing to a small fraction of a mas
// at this scale, far below this test's tolerances.

#include "EquipmentProfile.h"
#include "GaiaCatalog.h"
#include "ImageDater.h"
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

bool writeWcsSidecar(const QString &path, double crval1, double crval2, double crpix1,
                      double crpix2, double cdArcsecPerPix, int imageW, int imageH)
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
    double cd11 = -cdDeg, cd12 = 0.0, cd21 = 0.0, cd22 = cdDeg;
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

    // --- Reference (linear, undistorted) WCS -- same as this image's own
    // plate-solved .wcs sidecar would carry (no SIP terms). ---
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

    const QString imagePath = tmpDir.filePath("sub.fits");
    const QString wcsPath = tmpDir.filePath("sub.wcs");
    if (!writeWcsSidecar(wcsPath, crval1, crval2, crpix1, crpix2, scaleArcsecPerPix, width, height)) {
        out << "FAIL: could not write .wcs sidecar\n";
        return 1;
    }

    // --- Injected order-2 distortion (mas), same term ordering as
    // SipPolynomial.h -- used only for the "with profile" case. ---
    const double pixelScaleNorm = 200.0;
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

    // --- Star grid: fixed pixel positions (FITS 1-indexed) ---
    QVector<QPair<double, double>> gridStars;
    for (int gy = 0; gy < 6; ++gy) {
        for (int gx = 0; gx < 8; ++gx) {
            const double u = 30.0 + gx * 45.0;
            const double v = 25.0 + gy * 45.0;
            if (u > width - 20 || v > height - 20)
                continue;
            gridStars.push_back({u, v});
        }
    }
    out << "Star grid: " << gridStars.size() << " stars\n";

    const double trueEpochJyear = 2019.5;
    const double refEpochJyear = 2016.0;
    const double dtYears = trueEpochJyear - refEpochJyear;

    // --- Two synthetic Gaia catalogs sharing the same pixel grid and the
    // same per-star proper motions: "linear" (case A, no profile -- the
    // star's true position IS what the plain linear WCS predicts) and
    // "distorted" (case B, with profile -- the star's true position is
    // offset from the linear prediction by the injected correction, the
    // way a real rig's distortion would show up). ---
    QVector<GaiaStar> catalogLinear, catalogDistorted;
    for (int i = 0; i < gridStars.size(); ++i) {
        const auto [u, v] = gridStars[i];
        double raLin = 0.0, decLin = 0.0;
        if (!refWcs.pixToWorld(u, v, &raLin, &decLin)) {
            out << "FAIL: reference WCS pixToWorld failed while building the synthetic catalog\n";
            return 1;
        }
        const double cosDec = std::cos(decLin * M_PI / 180.0);

        // Deterministic per-star proper motion, tens of mas/yr, varied but
        // reproducible without needing <random> for this.
        const double pmra = 12.0 + 3.5 * (i % 7);
        const double pmdec = -9.0 + 2.7 * (i % 5);

        auto makeStar = [&](double raObs, double decObs) {
            GaiaStar star;
            star.sourceId = 2000 + i;
            star.refEpochJyear = refEpochJyear;
            // Walk the observed (trueEpoch) position backward by this
            // star's own proper motion to get its 2016.0 reference
            // position -- see the file comment for why this
            // tangent-plane inversion is accurate enough here.
            star.raDeg = raObs - (pmra * dtYears) / (cosDec * 3.6e6);
            star.decDeg = decObs - (pmdec * dtYears) / 3.6e6;
            star.pmraMasYr = pmra;
            star.pmdecMasYr = pmdec;
            star.hasParallax = false;
            star.hasRadialVelocity = false;
            star.photGMeanMag = 12.0;
            star.ruwe = 1.0;
            return star;
        };

        catalogLinear.push_back(makeStar(raLin, decLin));

        const auto [dxiMas, detaMas] = injectedCorrection(u - crpix1, v - crpix2);
        const double raDist = raLin + (dxiMas / 3.6e6) / cosDec;
        const double decDist = decLin + detaMas / 3.6e6;
        catalogDistorted.push_back(makeStar(raDist, decDist));
    }

    // --- Render the single light frame all cases share ---
    const double fwhmPx = 4.0;
    const double sigmaPx = fwhmPx / 2.3548200450309493;
    const double amplitude = 6000.0;
    const double backgroundLevel = 1000.0;
    const double noiseSigma = 12.0;

    QVector<float> pixels(width * height);
    std::mt19937 rng(2024);
    std::normal_distribution<double> noise(0.0, noiseSigma);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[y * width + x] = float(backgroundLevel + noise(rng));
    for (const auto &[gu, gv] : gridStars) {
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
    if (!writeLightFrame(imagePath, width, height, pixels, "2021-06-15T04:00:00")) {
        out << "FAIL: could not write synthetic light frame\n";
        return 1;
    }

    bool ok = true;

    // --- Case A: no equipment profile, linear catalog ---
    {
        DateEstimateOptions options;
        options.detection.fwhmPx = fwhmPx;
        options.detection.thresholdSigma = 6.0;
        options.equipmentProfile = nullptr;

        const DateEstimateResult r = ImageDater::estimate(imagePath, wcsPath, catalogLinear, options);
        out << QString("\n[A: no profile, linear catalog] ok=%1 detected=%2 used=%3 rms=%4mas "
                        "epoch=%5+/-%6 (true=%7)\n")
                   .arg(r.ok)
                   .arg(r.nStarsDetected)
                   .arg(r.nStarsUsed)
                   .arg(r.rmsResidualMas, 0, 'f', 1)
                   .arg(r.epochJyear, 0, 'f', 4)
                   .arg(r.epochSigmaYears, 0, 'f', 4)
                   .arg(trueEpochJyear, 0, 'f', 4);
        if (!r.ok) {
            out << "FAIL: case A estimate() failed: " << r.errorMessage << "\n";
            ok = false;
        } else {
            if (r.usedEquipmentProfile) {
                out << "FAIL: case A unexpectedly reports usedEquipmentProfile\n";
                ok = false;
            }
            if (r.nStarsUsed < gridStars.size() - 2) {
                out << "FAIL: case A matched far fewer stars than the grid has\n";
                ok = false;
            }
            const double errDays = (r.epochJyear - trueEpochJyear) * 365.25;
            const double sigmaDays = r.epochSigmaYears * 365.25;
            if (!(std::abs(errDays) < 5.0 * std::max(sigmaDays, 1.0))) {
                out << QString("FAIL: case A epoch error %1 d exceeds 5x its own sigma (%2 d)\n")
                           .arg(errDays, 0, 'f', 1)
                           .arg(sigmaDays, 0, 'f', 1);
                ok = false;
            }
            if (!(r.rmsResidualMas < 100.0)) {
                out << "FAIL: case A RMS residual implausibly large for an undistorted, "
                       "no-profile fit\n";
                ok = false;
            }
        }
    }

    // --- Case B: with equipment profile, distorted catalog ---
    EquipmentProfile profile;
    profile.crpix1 = crpix1;
    profile.crpix2 = crpix2;
    profile.pixelScaleNorm = pixelScaleNorm;
    profile.polyOrderChosen = 2;
    profile.polyCoeffsXi = trueCoeffsXi;
    profile.polyCoeffsEta = trueCoeffsEta;
    profile.rmsAfterHeldoutMas = 15.0; // stand-in "measured" precision, used as obsSigmaMas default
    double caseBRms = std::numeric_limits<double>::quiet_NaN();
    {
        DateEstimateOptions options;
        options.detection.fwhmPx = fwhmPx;
        options.detection.thresholdSigma = 6.0;
        options.equipmentProfile = &profile;

        const DateEstimateResult r = ImageDater::estimate(imagePath, wcsPath, catalogDistorted, options);
        out << QString("[B: with profile, distorted catalog] ok=%1 detected=%2 used=%3 rms=%4mas "
                        "epoch=%5+/-%6 (true=%7) obsSigma=%8mas\n")
                   .arg(r.ok)
                   .arg(r.nStarsDetected)
                   .arg(r.nStarsUsed)
                   .arg(r.rmsResidualMas, 0, 'f', 1)
                   .arg(r.epochJyear, 0, 'f', 4)
                   .arg(r.epochSigmaYears, 0, 'f', 4)
                   .arg(trueEpochJyear, 0, 'f', 4)
                   .arg(r.obsSigmaMasUsed, 0, 'f', 1);
        if (!r.ok) {
            out << "FAIL: case B estimate() failed: " << r.errorMessage << "\n";
            ok = false;
        } else {
            caseBRms = r.rmsResidualMas;
            if (!r.usedEquipmentProfile) {
                out << "FAIL: case B does not report usedEquipmentProfile\n";
                ok = false;
            }
            if (!(std::abs(r.obsSigmaMasUsed - profile.rmsAfterHeldoutMas) < 1e-6)) {
                out << "FAIL: case B did not default obsSigmaMas to the profile's held-out RMS\n";
                ok = false;
            }
            const double errDays = (r.epochJyear - trueEpochJyear) * 365.25;
            const double sigmaDays = r.epochSigmaYears * 365.25;
            if (!(std::abs(errDays) < 5.0 * std::max(sigmaDays, 1.0))) {
                out << QString("FAIL: case B epoch error %1 d exceeds 5x its own sigma (%2 d)\n")
                           .arg(errDays, 0, 'f', 1)
                           .arg(sigmaDays, 0, 'f', 1);
                ok = false;
            }
            if (!(r.rmsResidualMas < 100.0)) {
                out << "FAIL: case B RMS residual implausibly large -- profile correction did not "
                       "recover the injected distortion\n";
                ok = false;
            }
        }
    }

    // --- Cross-check: the distorted catalog run WITHOUT the profile should
    // show a much larger residual than case B did -- proof the profile
    // correction is actually doing something, not just along for the ride.
    {
        DateEstimateOptions options;
        options.detection.fwhmPx = fwhmPx;
        options.detection.thresholdSigma = 6.0;
        options.equipmentProfile = nullptr;

        const DateEstimateResult r = ImageDater::estimate(imagePath, wcsPath, catalogDistorted, options);
        out << QString("[cross-check: no profile, distorted catalog] ok=%1 rms=%2mas\n")
                   .arg(r.ok)
                   .arg(r.rmsResidualMas, 0, 'f', 1);
        if (!r.ok) {
            out << "FAIL: cross-check estimate() failed: " << r.errorMessage << "\n";
            ok = false;
        } else if (!std::isfinite(caseBRms) || !(r.rmsResidualMas > caseBRms * 3.0)) {
            out << "FAIL: expected a much larger residual without the profile correcting the "
                   "same injected distortion\n";
            ok = false;
        }
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
