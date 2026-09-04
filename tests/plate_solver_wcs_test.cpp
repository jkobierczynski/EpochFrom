// Unit test for PlateSolver::readWcsFile()'s WCS math, using a synthetic
// headers-only FITS file built with known CRVAL/CRPIX/CD/IMAGEW/IMAGEH
// values (astrometry.net's own .wcs sidecar convention) rather than a large
// real fixture -- this way the test doesn't depend on solve-field being
// installed, and the expected answer can be hand-computed rather than
// eyeballed against a real solve.

#include "PlateSolver.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTemporaryDir>
#include <QFile>

#include <fitsio.h>

#include <cmath>
#include <cstdio>

using namespace epochfrom;

namespace {

// Writes a minimal headers-only FITS file (no image data, NAXIS=0 -- same
// shape as an astrometry.net .wcs sidecar) with a TAN projection centered
// at (crvalRa, crvalDec), a diagonal CD matrix (no rotation, uniform
// pixel scale), and IMAGEW/IMAGEH for the field dimensions.
bool writeSyntheticWcsFile(const QString &path, double crvalRa, double crvalDec, double crpix1,
                            double crpix2, double cdArcsecPerPix, int imageW, int imageH,
                            QString *errorMessage)
{
    QFile::remove(path); // cfitsio's "!" overwrite prefix is simpler than checking existence
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
    double cd11 = -cdDeg, cd12 = 0.0, cd21 = 0.0, cd22 = cdDeg; // RA flips with pixel x, by convention
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

bool nearlyEqual(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
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
    const QString wcsPath = tmpDir.filePath("synthetic.wcs");

    // Field center at RA=180, Dec=+45 (away from the RA=0 wraparound and the
    // pole, so the expected answer is simple), CRPIX at the exact image
    // center so the center pixel maps back to CRVAL exactly, 1.0"/px scale,
    // a 4000x3000 image -> field size should be exactly
    // 4000/60 = 66.6667 x 3000/60 = 50.0 arcmin.
    const double crvalRa = 180.0;
    const double crvalDec = 45.0;
    const int imageW = 4000;
    const int imageH = 3000;
    const double crpix1 = (imageW + 1.0) / 2.0;
    const double crpix2 = (imageH + 1.0) / 2.0;
    const double scaleArcsecPerPix = 1.0;

    QString err;
    if (!writeSyntheticWcsFile(wcsPath, crvalRa, crvalDec, crpix1, crpix2, scaleArcsecPerPix,
                                imageW, imageH, &err)) {
        out << "FAIL: " << err << "\n";
        return 1;
    }

    const PlateSolveResult r = PlateSolver::readWcsFile(wcsPath);
    if (!r.solved) {
        out << "FAIL: readWcsFile did not solve: " << r.errorMessage << "\n";
        return 1;
    }

    bool ok = true;

    out << QString("center RA=%1 Dec=%2 (expected %3 %4)\n")
               .arg(r.centerRaDeg, 0, 'f', 6)
               .arg(r.centerDecDeg, 0, 'f', 6)
               .arg(crvalRa, 0, 'f', 6)
               .arg(crvalDec, 0, 'f', 6);
    if (!nearlyEqual(r.centerRaDeg, crvalRa, 1e-4)) {
        out << "FAIL: center RA off by more than 1e-4 deg\n";
        ok = false;
    }
    if (!nearlyEqual(r.centerDecDeg, crvalDec, 1e-4)) {
        out << "FAIL: center Dec off by more than 1e-4 deg\n";
        ok = false;
    }

    out << QString("pixel scale=%1 \"/px (expected %2)\n")
               .arg(r.pixelScaleArcsecPerPix, 0, 'f', 6)
               .arg(scaleArcsecPerPix, 0, 'f', 6);
    if (!nearlyEqual(r.pixelScaleArcsecPerPix, scaleArcsecPerPix, 1e-6)) {
        out << "FAIL: pixel scale mismatch\n";
        ok = false;
    }

    const double expectedWidthArcmin = imageW * scaleArcsecPerPix / 60.0;
    const double expectedHeightArcmin = imageH * scaleArcsecPerPix / 60.0;
    out << QString("field=%1 x %2 arcmin (expected %3 x %4)\n")
               .arg(r.fieldWidthArcmin, 0, 'f', 4)
               .arg(r.fieldHeightArcmin, 0, 'f', 4)
               .arg(expectedWidthArcmin, 0, 'f', 4)
               .arg(expectedHeightArcmin, 0, 'f', 4);
    if (!nearlyEqual(r.fieldWidthArcmin, expectedWidthArcmin, 1e-6)) {
        out << "FAIL: field width mismatch\n";
        ok = false;
    }
    if (!nearlyEqual(r.fieldHeightArcmin, expectedHeightArcmin, 1e-6)) {
        out << "FAIL: field height mismatch\n";
        ok = false;
    }

    if (r.imageWidthPx != imageW || r.imageHeightPx != imageH) {
        out << QString("FAIL: image dimensions %1x%2, expected %3x%4\n")
                   .arg(r.imageWidthPx)
                   .arg(r.imageHeightPx)
                   .arg(imageW)
                   .arg(imageH);
        ok = false;
    }

    // Also check a non-existent file is reported as a clean failure, not a
    // crash -- a plausible real-world call pattern (user points --wcs at
    // the wrong path).
    const PlateSolveResult missing = PlateSolver::readWcsFile(tmpDir.filePath("does_not_exist.wcs"));
    if (missing.solved || missing.errorMessage.isEmpty()) {
        out << "FAIL: reading a missing WCS file should fail cleanly with an error message\n";
        ok = false;
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
