// Unit test for PlateSolver::writeWcsIntoFits() -- the opt-in step behind
// PlateSolveOptions::updateFitsHeader that copies a solved WCS (and SIP
// terms, if present) from a .wcs sidecar into the original image's own
// FITS header, in place. Builds a synthetic "light frame" FITS file and a
// synthetic .wcs sidecar (one with a SIP distortion, one without) so this
// doesn't depend on solve-field being installed.

#include "PlateSolver.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QTemporaryDir>
#include <QFile>

#include <fitsio.h>

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace epochfrom;

namespace {

QString cfitsioErr(int status)
{
    char buf[FLEN_STATUS];
    fits_get_errstatus(status, buf);
    return QString::fromLocal8Bit(buf);
}

// A tiny (2x2 pixel) "light frame" -- enough to be a real image HDU with
// its own DATE-OBS and OBJCTRA/OBJCTDEC, which writeWcsIntoFits must leave
// alone.
bool writeSyntheticLightFrame(const QString &path, QString *err)
{
    QFile::remove(path);
    int status = 0;
    fitsfile *fptr = nullptr;
    const QString createPath = "!" + path;
    if (fits_create_file(&fptr, createPath.toLocal8Bit().constData(), &status) != 0) {
        *err = cfitsioErr(status);
        return false;
    }
    long naxes[2] = {2, 2};
    fits_create_img(fptr, USHORT_IMG, 2, naxes, &status);
    unsigned short pixels[4] = {1, 2, 3, 4};
    fits_write_img(fptr, TUSHORT, 1, 4, pixels, &status);

    char dateObs[] = "2024-09-01T02:30:00";
    fits_write_key(fptr, TSTRING, "DATE-OBS", dateObs, "UTC start of exposure", &status);
    char objctra[] = "20 59 42.5"; // NGC 7000-ish, sexagesimal hours
    char objctdec[] = "+44 20 00";
    fits_write_key(fptr, TSTRING, "OBJCTRA", objctra, "mount's requested pointing (RA)", &status);
    fits_write_key(fptr, TSTRING, "OBJCTDEC", objctdec, "mount's requested pointing (Dec)", &status);

    fits_close_file(fptr, &status);
    if (status != 0) {
        *err = cfitsioErr(status);
        return false;
    }
    return true;
}

// A headers-only .wcs sidecar (astrometry.net's own output shape), with an
// optional SIP-A/B distortion pair of order 2.
bool writeSyntheticWcs(const QString &path, double crvalRa, double crvalDec, bool withSip,
                       QString *err)
{
    QFile::remove(path);
    int status = 0;
    fitsfile *fptr = nullptr;
    const QString createPath = "!" + path;
    if (fits_create_file(&fptr, createPath.toLocal8Bit().constData(), &status) != 0) {
        *err = cfitsioErr(status);
        return false;
    }
    long naxes[1] = {0};
    fits_create_img(fptr, SHORT_IMG, 0, naxes, &status);

    long imageW = 2, imageH = 2;
    fits_write_key(fptr, TLONG, "IMAGEW", &imageW, "", &status);
    fits_write_key(fptr, TLONG, "IMAGEH", &imageH, "", &status);

    // Sized for the longer "-SIP" variant regardless of which one ends up
    // used, so there's no risk of writing past the array.
    char ctype1[16] = "RA---TAN";
    char ctype2[16] = "DEC--TAN";
    if (withSip) {
        // solve-field spells these RA---TAN-SIP / DEC--TAN-SIP when SIP
        // terms are present -- doesn't matter to writeWcsIntoFits (it just
        // copies whatever CTYPE1/2 card exists), but keep it realistic.
        std::strcpy(ctype1, "RA---TAN-SIP");
        std::strcpy(ctype2, "DEC--TAN-SIP");
    }
    fits_write_key(fptr, TSTRING, "CTYPE1", ctype1, "", &status);
    fits_write_key(fptr, TSTRING, "CTYPE2", ctype2, "", &status);
    double crpix1 = 1.5, crpix2 = 1.5;
    fits_write_key(fptr, TDOUBLE, "CRPIX1", &crpix1, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRPIX2", &crpix2, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL1", &crvalRa, "", &status);
    fits_write_key(fptr, TDOUBLE, "CRVAL2", &crvalDec, "", &status);
    double cd11 = -0.0002778, cd12 = 0.0, cd21 = 0.0, cd22 = 0.0002778;
    fits_write_key(fptr, TDOUBLE, "CD1_1", &cd11, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD1_2", &cd12, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD2_1", &cd21, "", &status);
    fits_write_key(fptr, TDOUBLE, "CD2_2", &cd22, "", &status);
    char cunit[] = "deg";
    fits_write_key(fptr, TSTRING, "CUNIT1", cunit, "", &status);
    fits_write_key(fptr, TSTRING, "CUNIT2", cunit, "", &status);

    if (withSip) {
        long order = 2;
        fits_write_key(fptr, TLONG, "A_ORDER", &order, "", &status);
        fits_write_key(fptr, TLONG, "B_ORDER", &order, "", &status);
        double a00 = 0.0, a11 = 1.23e-6, a20 = -4.5e-7;
        fits_write_key(fptr, TDOUBLE, "A_0_0", &a00, "", &status);
        fits_write_key(fptr, TDOUBLE, "A_1_1", &a11, "", &status);
        fits_write_key(fptr, TDOUBLE, "A_2_0", &a20, "", &status);
        double b00 = 0.0, b11 = -2.1e-6;
        fits_write_key(fptr, TDOUBLE, "B_0_0", &b00, "", &status);
        fits_write_key(fptr, TDOUBLE, "B_1_1", &b11, "", &status);
    }

    fits_close_file(fptr, &status);
    if (status != 0) {
        *err = cfitsioErr(status);
        return false;
    }
    return true;
}

bool readStringKey(const QString &path, const char *key, QString *out)
{
    int status = 0;
    fitsfile *fptr = nullptr;
    if (fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status) != 0)
        return false;
    char value[FLEN_VALUE];
    if (fits_read_key(fptr, TSTRING, key, value, nullptr, &status) != 0) {
        fits_close_file(fptr, &status);
        return false;
    }
    *out = QString::fromLocal8Bit(value);
    fits_close_file(fptr, &status);
    return true;
}

bool readDoubleKey(const QString &path, const char *key, double *out)
{
    int status = 0;
    fitsfile *fptr = nullptr;
    if (fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status) != 0)
        return false;
    if (fits_read_key(fptr, TDOUBLE, key, out, nullptr, &status) != 0) {
        fits_close_file(fptr, &status);
        return false;
    }
    fits_close_file(fptr, &status);
    return true;
}

bool keyExists(const QString &path, const char *key)
{
    int status = 0;
    fitsfile *fptr = nullptr;
    if (fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status) != 0)
        return false;
    char card[FLEN_CARD];
    const bool present = fits_read_card(fptr, key, card, &status) == 0;
    int closeStatus = 0;
    fits_close_file(fptr, &closeStatus);
    return present;
}

bool hasHistoryMentioning(const QString &path, const QString &needle)
{
    int status = 0;
    fitsfile *fptr = nullptr;
    if (fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status) != 0)
        return false;
    int nkeys = 0, more = 0;
    fits_get_hdrspace(fptr, &nkeys, &more, &status);
    bool found = false;
    for (int i = 1; i <= nkeys; ++i) {
        char card[FLEN_CARD];
        if (fits_read_record(fptr, i, card, &status) != 0)
            break;
        if (QString::fromLocal8Bit(card).contains(needle)) {
            found = true;
            break;
        }
    }
    int closeStatus = 0;
    fits_close_file(fptr, &closeStatus);
    return found;
}

bool nearlyEqual(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

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

    bool ok = true;
    const double crvalRa = 314.925;
    const double crvalDec = 43.664;

    // --- Case 1: WCS with SIP terms -----------------------------------
    {
        const QString imagePath = tmpDir.filePath("light_sip.fits");
        const QString wcsPath = tmpDir.filePath("light_sip.wcs");
        QString err;
        if (!writeSyntheticLightFrame(imagePath, &err) ||
            !writeSyntheticWcs(wcsPath, crvalRa, crvalDec, /*withSip=*/true, &err)) {
            out << "FAIL: setup: " << err << "\n";
            return 1;
        }

        PlateSolveResult result = PlateSolver::readWcsFile(wcsPath);
        if (!result.solved) {
            out << "FAIL: readWcsFile on synthetic SIP .wcs didn't solve: " << result.errorMessage
                << "\n";
            return 1;
        }

        const QString warning = PlateSolver::writeWcsIntoFits(imagePath, wcsPath, result);
        if (!warning.isEmpty()) {
            out << "FAIL: writeWcsIntoFits reported a warning: " << warning << "\n";
            ok = false;
        }

        double crval1 = 0.0, crval2 = 0.0, ra = 0.0, dec = 0.0, a11 = 0.0, b11 = 0.0;
        if (!readDoubleKey(imagePath, "CRVAL1", &crval1) || !readDoubleKey(imagePath, "CRVAL2", &crval2)) {
            out << "FAIL: CRVAL1/2 not copied into the image header\n";
            ok = false;
        } else if (!nearlyEqual(crval1, crvalRa, 1e-9) || !nearlyEqual(crval2, crvalDec, 1e-9)) {
            out << "FAIL: copied CRVAL1/2 don't match the .wcs sidecar\n";
            ok = false;
        }
        if (!readDoubleKey(imagePath, "RA", &ra) || !readDoubleKey(imagePath, "DEC", &dec)) {
            out << "FAIL: convenience RA/DEC keys not written\n";
            ok = false;
        } else if (!nearlyEqual(ra, result.centerRaDeg, 1e-6) ||
                   !nearlyEqual(dec, result.centerDecDeg, 1e-6)) {
            out << "FAIL: convenience RA/DEC keys don't match the solved center\n";
            ok = false;
        }
        if (!readDoubleKey(imagePath, "A_1_1", &a11) || !nearlyEqual(a11, 1.23e-6, 1e-12)) {
            out << "FAIL: SIP term A_1_1 not copied correctly\n";
            ok = false;
        }
        if (!readDoubleKey(imagePath, "B_1_1", &b11) || !nearlyEqual(b11, -2.1e-6, 1e-12)) {
            out << "FAIL: SIP term B_1_1 not copied correctly\n";
            ok = false;
        }
        if (keyExists(imagePath, "A_ORDER") == false) {
            out << "FAIL: A_ORDER not copied\n";
            ok = false;
        }

        // OBJCTRA/OBJCTDEC -- the mount's own requested-pointing record --
        // must survive completely untouched.
        QString objctra;
        if (!readStringKey(imagePath, "OBJCTRA", &objctra) || objctra.trimmed() != "20 59 42.5") {
            out << "FAIL: OBJCTRA was touched or lost (got '" << objctra << "')\n";
            ok = false;
        }

        if (!hasHistoryMentioning(imagePath, "EpochFrom")) {
            out << "FAIL: no HISTORY card recording the header update\n";
            ok = false;
        }
        if (!keyExists(imagePath, "CHECKSUM")) {
            out << "FAIL: CHECKSUM not written\n";
            ok = false;
        }

        out << "Case 1 (with SIP): " << (ok ? "ok so far\n" : "FAILED (see above)\n");
    }

    // --- Case 2: WCS without SIP terms -- families must be skipped cleanly
    {
        const QString imagePath = tmpDir.filePath("light_nosip.fits");
        const QString wcsPath = tmpDir.filePath("light_nosip.wcs");
        QString err;
        if (!writeSyntheticLightFrame(imagePath, &err) ||
            !writeSyntheticWcs(wcsPath, crvalRa, crvalDec, /*withSip=*/false, &err)) {
            out << "FAIL: setup: " << err << "\n";
            return 1;
        }
        PlateSolveResult result = PlateSolver::readWcsFile(wcsPath);
        if (!result.solved) {
            out << "FAIL: readWcsFile on synthetic non-SIP .wcs didn't solve: "
                << result.errorMessage << "\n";
            return 1;
        }
        const QString warning = PlateSolver::writeWcsIntoFits(imagePath, wcsPath, result);
        if (!warning.isEmpty()) {
            out << "FAIL: writeWcsIntoFits reported a warning on a non-SIP file: " << warning << "\n";
            ok = false;
        }
        if (keyExists(imagePath, "A_ORDER")) {
            out << "FAIL: A_ORDER appeared even though the .wcs sidecar had no SIP terms\n";
            ok = false;
        }
        double crval1 = 0.0;
        if (!readDoubleKey(imagePath, "CRVAL1", &crval1) || !nearlyEqual(crval1, crvalRa, 1e-9)) {
            out << "FAIL: CRVAL1 missing/wrong in the no-SIP case\n";
            ok = false;
        }
        out << "Case 2 (without SIP): " << (ok ? "ok so far\n" : "FAILED (see above)\n");
    }

    // --- Case 3: missing image file reports a clean warning, not a crash
    {
        const QString wcsPath = tmpDir.filePath("light_sip.wcs"); // reuse from case 1
        PlateSolveResult result = PlateSolver::readWcsFile(wcsPath);
        const QString warning = PlateSolver::writeWcsIntoFits(tmpDir.filePath("does_not_exist.fits"),
                                                                wcsPath, result);
        if (warning.isEmpty()) {
            out << "FAIL: writing into a nonexistent image should report a warning\n";
            ok = false;
        } else {
            out << "Case 3 (missing image): got expected warning: " << warning << "\n";
        }
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
