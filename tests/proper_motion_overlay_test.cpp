// Unit test for ProperMotionOverlay::compute() (and a light spot-check of
// ImageStretch::autoStretch()), using a synthetic headers-only .wcs fixture
// -- same technique plate_solver_wcs_test.cpp uses -- plus hand-built Gaia
// stars whose expected pixel-space motion direction can be derived from the
// WCS's own CD matrix convention, rather than by calling the code under
// test to produce its own "expected" answer.

#include "FitsImage.h"
#include "ImageStretch.h"
#include "ProperMotionOverlay.h"
#include "Wcs.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include <fitsio.h>

#include <cmath>

using namespace epochfrom;

namespace {

bool nearlyEqual(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

// Same synthetic-.wcs fixture writer plate_solver_wcs_test.cpp uses: a
// headers-only FITS file (astrometry.net .wcs sidecar shape) with a TAN
// projection, no rotation, diagonal CD matrix (RA flips with pixel x, the
// standard "East is left, North is up" orientation).
bool writeSyntheticWcsFile(const QString &path, double crvalRa, double crvalDec, double crpix1,
                            double crpix2, double cdArcsecPerPix, int imageW, int imageH,
                            QString *errorMessage)
{
    QFile::remove(path);
    int status = 0;
    fitsfile *fptr = nullptr;
    const QString createPath = "!" + path;
    if (fits_create_file(&fptr, createPath.toLocal8Bit().constData(), &status) != 0) {
        char buf[FLEN_STATUS];
        fits_get_errstatus(status, buf);
        *errorMessage = QStringLiteral("fits_create_file: %1").arg(QString::fromLocal8Bit(buf));
        return false;
    }

    long naxes[1] = {0};
    fits_create_img(fptr, SHORT_IMG, 0, naxes, &status);

    const double cdDeg = cdArcsecPerPix / 3600.0;
    long lval;
    lval = imageW;
    fits_write_key(fptr, TLONG, "IMAGEW", &lval, "image width, px", &status);
    lval = imageH;
    fits_write_key(fptr, TLONG, "IMAGEH", &lval, "image height, px", &status);

    char ctype1[] = "RA---TAN";
    char ctype2[] = "DEC--TAN";
    fits_write_key(fptr, TSTRING, "CTYPE1", ctype1, "", &status);
    fits_write_key(fptr, TSTRING, "CTYPE2", ctype2, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL1", &crvalRa, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL2", &crvalDec, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX1", &crpix1, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX2", &crpix2, "", &status);
    double cd11 = -cdDeg, cd12 = 0.0, cd21 = 0.0, cd22 = cdDeg;
    fits_write_key(fptr, TDOUBLE, "CD1_1", &cd11, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD1_2", &cd12, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD2_1", &cd21, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD2_2", &cd22, "", &status);
    char cunit[] = "deg";
    fits_write_key(fptr, TSTRING, "CUNIT1", cunit, "", &status);
    fits_write_key(fptr, TSTRING, "CUNIT2", cunit, "", &status);

    fits_close_file(fptr, &status);
    if (status != 0) {
        char buf[FLEN_STATUS];
        fits_get_errstatus(status, buf);
        *errorMessage = QStringLiteral("cfitsio error while writing: %1").arg(QString::fromLocal8Bit(buf));
        return false;
    }
    return true;
}

GaiaStar makeStar(qint64 id, double raDeg, double decDeg, double pmra, double pmdec)
{
    GaiaStar star;
    star.sourceId = id;
    star.refEpochJyear = 2016.0;
    star.raDeg = raDeg;
    star.decDeg = decDeg;
    star.pmraMasYr = pmra;
    star.pmdecMasYr = pmdec;
    star.hasParallax = false;
    star.hasRadialVelocity = false;
    star.photGMeanMag = 12.0;
    return star;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    bool ok = true;

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        out << "FAIL: could not create temp dir\n";
        return 1;
    }
    const QString wcsPath = tmpDir.filePath("synthetic.wcs");

    const double crvalRa = 180.0;
    const double crvalDec = 45.0;
    const int imageW = 4000;
    const int imageH = 3000;
    const double crpix1 = (imageW + 1.0) / 2.0;
    const double crpix2 = (imageH + 1.0) / 2.0;
    const double scaleArcsecPerPix = 1.0;

    QString err;
    if (!writeSyntheticWcsFile(wcsPath, crvalRa, crvalDec, crpix1, crpix2, scaleArcsecPerPix, imageW,
                                imageH, &err)) {
        out << "FAIL: " << err << "\n";
        return 1;
    }

    const Wcs wcs(wcsPath);
    if (!wcs.isValid()) {
        out << "FAIL: synthetic WCS did not parse: " << wcs.errorMessage() << "\n";
        return 1;
    }

    // --- Direction geometry --------------------------------------------
    // Two stars sitting exactly at the tangent point (field center), each
    // moving along one axis only. With this fixture's CD convention
    // (cd11=-cdDeg, cd22=+cdDeg -- East left, North up), a pure-pmra
    // (eastward) mover should walk toward DEcreasing pixel X with
    // negligible pixel-Y drift, and a pure-pmdec (northward) mover should
    // walk toward INcreasing pixel Y with negligible pixel-X drift -- see
    // the file comment for the tangent-plane derivation. Give both a huge
    // proper motion (2000 mas/yr) so they'd land far outside a real Gaia
    // catalog, but that's fine here: it just makes the direction over the
    // 200-year default baseline easy to reason about, and 2000 mas/yr over
    // 200 yr is still only ~0.11 deg -- tiny compared to this WCS's
    // near-90-degree-scale linearity, so the "negligible" cross-axis
    // component should be at the 1e-3 pixel-unit level, not a fraction of
    // a percent.
    QVector<GaiaStar> directionStars = {
        makeStar(1, crvalRa, crvalDec, 2000.0, 0.0), // pure East
        makeStar(2, crvalRa, crvalDec, 0.0, 2000.0), // pure North
    };

    ProperMotionOverlayOptions dirOptions;
    dirOptions.topN = 2;
    dirOptions.targetEpochJyear = 2016.0;
    dirOptions.imageWidth = imageW;
    dirOptions.imageHeight = imageH;
    dirOptions.averageArrowLengthPix = 100.0;

    QString dirWarning;
    const QVector<ProperMotionArrow> dirArrows =
        ProperMotionOverlay::compute(directionStars, wcs, dirOptions, &dirWarning);
    if (dirArrows.size() != 2) {
        out << "FAIL: expected 2 direction-test arrows, got " << dirArrows.size() << " (warning: " << dirWarning
            << ")\n";
        ok = false;
    } else {
        const ProperMotionArrow *eastArrow = nullptr;
        const ProperMotionArrow *northArrow = nullptr;
        for (const ProperMotionArrow &a : dirArrows) {
            if (a.sourceId == 1)
                eastArrow = &a;
            else if (a.sourceId == 2)
                northArrow = &a;
        }
        if (!eastArrow || !northArrow) {
            out << "FAIL: couldn't find both direction-test stars in the result\n";
            ok = false;
        } else {
            out << QString("east mover dir = (%1, %2) (expect ~(-1, 0))\n")
                       .arg(eastArrow->dirPixX, 0, 'f', 6)
                       .arg(eastArrow->dirPixY, 0, 'f', 6);
            if (!nearlyEqual(eastArrow->dirPixX, -1.0, 1e-3) || !nearlyEqual(eastArrow->dirPixY, 0.0, 1e-3)) {
                out << "FAIL: east-moving star's direction should be ~(-1, 0) in pixel space\n";
                ok = false;
            }

            out << QString("north mover dir = (%1, %2) (expect ~(0, 1))\n")
                       .arg(northArrow->dirPixX, 0, 'f', 6)
                       .arg(northArrow->dirPixY, 0, 'f', 6);
            if (!nearlyEqual(northArrow->dirPixX, 0.0, 1e-3) || !nearlyEqual(northArrow->dirPixY, 1.0, 1e-3)) {
                out << "FAIL: north-moving star's direction should be ~(0, 1) in pixel space\n";
                ok = false;
            }
        }
    }

    // --- Frame-bounds filtering + topN ranking + length scaling ---------
    // Five in-frame stars with distinct pm magnitudes, plus one huge-pm
    // star placed way outside the frame (a plausible "wide Gaia query cone
    // vs. narrow sensor footprint" situation) -- it must NOT show up in the
    // top-3 despite having the largest raw proper motion in the catalog.
    QVector<GaiaStar> rankStars = {
        makeStar(101, crvalRa, crvalDec, 10.0, 0.0),       // pm = 10
        makeStar(102, crvalRa, crvalDec, 40.0, 0.0),       // pm = 40
        makeStar(103, crvalRa, crvalDec, 25.0, 0.0),       // pm = 25
        makeStar(104, crvalRa, crvalDec, 5.0, 0.0),        // pm = 5
        makeStar(105, crvalRa, crvalDec, 70.0, 0.0),       // pm = 70 (largest in-frame)
        makeStar(999, crvalRa + 30.0, crvalDec, 500.0, 0.0), // way outside the frame, largest pm overall
    };

    ProperMotionOverlayOptions rankOptions;
    rankOptions.topN = 3;
    rankOptions.targetEpochJyear = 2016.0;
    rankOptions.imageWidth = imageW;
    rankOptions.imageHeight = imageH;
    rankOptions.averageArrowLengthPix = 90.0;

    QString rankWarning;
    const QVector<ProperMotionArrow> rankArrows =
        ProperMotionOverlay::compute(rankStars, wcs, rankOptions, &rankWarning);

    if (rankArrows.size() != 3) {
        out << "FAIL: expected top 3 arrows, got " << rankArrows.size() << "\n";
        ok = false;
    } else {
        const QVector<qint64> expectedIds = {105, 102, 103}; // pm 70, 40, 25 -- descending
        for (int i = 0; i < 3; ++i) {
            if (rankArrows[i].sourceId != expectedIds[i]) {
                out << QString("FAIL: rank %1 expected source %2, got %3\n")
                           .arg(i)
                           .arg(expectedIds[i])
                           .arg(rankArrows[i].sourceId);
                ok = false;
            }
        }
        if (rankArrows[0].pmTotalMasYr <= rankArrows[1].pmTotalMasYr ||
            rankArrows[1].pmTotalMasYr <= rankArrows[2].pmTotalMasYr) {
            out << "FAIL: arrows should be sorted by descending proper motion\n";
            ok = false;
        }

        // Average length across the selected set should equal the
        // requested average exactly (that's the whole point of the
        // scaling formula), and each individual length should be
        // proportional to that star's own pm.
        double sumLen = 0.0;
        for (const ProperMotionArrow &a : rankArrows)
            sumLen += a.lengthPix;
        const double meanLen = sumLen / rankArrows.size();
        out << QString("mean arrow length = %1 (expect %2)\n")
                   .arg(meanLen, 0, 'f', 4)
                   .arg(rankOptions.averageArrowLengthPix, 0, 'f', 4);
        if (!nearlyEqual(meanLen, rankOptions.averageArrowLengthPix, 1e-6)) {
            out << "FAIL: mean arrow length should equal averageArrowLengthPix\n";
            ok = false;
        }
        // pm=70 star's line should be exactly 70/40 times the pm=40 star's.
        const double ratio = rankArrows[0].lengthPix / rankArrows[1].lengthPix;
        if (!nearlyEqual(ratio, 70.0 / 40.0, 1e-6)) {
            out << "FAIL: arrow lengths should scale proportionally with pm_total\n";
            ok = false;
        }
    }

    // topN larger than what's available in-frame should still return
    // everything in-frame and set a non-fatal warning.
    ProperMotionOverlayOptions overshootOptions = rankOptions;
    overshootOptions.topN = 50;
    QString overshootWarning;
    const QVector<ProperMotionArrow> overshootArrows =
        ProperMotionOverlay::compute(rankStars, wcs, overshootOptions, &overshootWarning);
    if (overshootArrows.size() != 5) {
        out << "FAIL: expected all 5 in-frame stars when topN exceeds availability, got "
            << overshootArrows.size() << "\n";
        ok = false;
    }
    if (overshootWarning.isEmpty()) {
        out << "FAIL: expected a non-fatal warning when topN exceeds available in-frame stars\n";
        ok = false;
    } else {
        out << "overshoot warning: " << overshootWarning << "\n";
    }

    // Empty catalog / nothing in frame -> empty result + warning, not a
    // crash.
    QVector<GaiaStar> noneInFrame = {makeStar(1, crvalRa + 30.0, crvalDec, 100.0, 0.0)};
    QString emptyWarning;
    const QVector<ProperMotionArrow> emptyArrows =
        ProperMotionOverlay::compute(noneInFrame, wcs, rankOptions, &emptyWarning);
    if (!emptyArrows.isEmpty() || emptyWarning.isEmpty()) {
        out << "FAIL: expected an empty result with a warning when no star lands in frame\n";
        ok = false;
    }

    // --- ImageStretch spot-check -----------------------------------------
    FitsImageData flat;
    flat.loaded = true;
    flat.width = 4;
    flat.height = 4;
    flat.pixels = QVector<float>(16, 500.0f); // perfectly flat image

    const QVector<unsigned char> flatStretched = ImageStretch::autoStretch(flat);
    if (flatStretched.size() != 16) {
        out << "FAIL: ImageStretch output size should match width*height\n";
        ok = false;
    } else {
        for (unsigned char v : flatStretched) {
            if (v != 128) {
                out << "FAIL: a perfectly flat image should stretch to a constant mid-gray (128)\n";
                ok = false;
                break;
            }
        }
    }

    FitsImageData ramp;
    ramp.loaded = true;
    ramp.width = 100;
    ramp.height = 1;
    ramp.pixels.resize(100);
    for (int i = 0; i < 100; ++i)
        ramp.pixels[i] = static_cast<float>(i); // 0..99, uniform histogram

    const QVector<unsigned char> rampStretched = ImageStretch::autoStretch(ramp);
    if (rampStretched.size() != 100) {
        out << "FAIL: ImageStretch ramp output size mismatch\n";
        ok = false;
    } else {
        // Monotonic non-decreasing (asinh of a linear ramp is still
        // monotonic), spanning close to the full 0-255 range given a wide
        // default percentile clip on a clean uniform ramp.
        for (int i = 1; i < 100; ++i) {
            if (rampStretched[i] < rampStretched[i - 1]) {
                out << "FAIL: stretched ramp should be monotonic non-decreasing\n";
                ok = false;
                break;
            }
        }
        if (rampStretched.front() > 5 || rampStretched.back() < 250) {
            out << QString("FAIL: stretched ramp should span close to 0-255, got %1..%2\n")
                       .arg(rampStretched.front())
                       .arg(rampStretched.back());
            ok = false;
        }
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
