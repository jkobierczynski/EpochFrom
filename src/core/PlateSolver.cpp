#include "PlateSolver.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#include <fitsio.h>
#include <wcs.h>
#include <wcshdr.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace epochfrom {

namespace {

// Reads one numeric header key as a double, regardless of whether cfitsio
// thinks it's stored as an integer or a float in the file -- astrometry.net
// writes IMAGEW/IMAGEH as plain integers, but reading them via TDOUBLE works
// either way and saves having to try two datatypes.
bool readNumericKey(fitsfile *fptr, const char *key, double *outValue)
{
    int status = 0;
    char comment[FLEN_COMMENT];
    if (fits_read_key(fptr, TDOUBLE, key, outValue, comment, &status) != 0)
        return false;
    return true;
}

QString cfitsioErrorText(int status)
{
    char buf[FLEN_STATUS];
    fits_get_errstatus(status, buf);
    return QString::fromLocal8Bit(buf);
}

} // namespace

PlateSolveResult PlateSolver::readWcsFile(const QString &wcsPath)
{
    PlateSolveResult result;
    result.wcsFilePath = wcsPath;

    if (!QFileInfo::exists(wcsPath)) {
        result.errorMessage = QStringLiteral("WCS file does not exist: %1").arg(wcsPath);
        return result;
    }

    int status = 0;
    fitsfile *fptr = nullptr;
    // solve-field's .wcs sidecar is a headers-only FITS file, but cfitsio is
    // happy to open it as an ordinary FITS file -- there's just no image
    // data extension to go with the header.
    if (fits_open_file(&fptr, wcsPath.toLocal8Bit().constData(), READONLY, &status) != 0) {
        result.errorMessage =
            QStringLiteral("failed to open WCS file: %1").arg(cfitsioErrorText(status));
        return result;
    }

    // astrometry.net .wcs sidecars carry IMAGEW/IMAGEH for the solved
    // image's pixel dimensions -- NOT NAXIS1/NAXIS2, which are absent
    // (confirmed empirically: this is a headers-only file, there's no pixel
    // array for NAXIS to describe).
    double imageWidth = 0.0;
    double imageHeight = 0.0;
    if (!readNumericKey(fptr, "IMAGEW", &imageWidth) ||
        !readNumericKey(fptr, "IMAGEH", &imageHeight)) {
        result.errorMessage = QStringLiteral(
            "WCS file is missing IMAGEW/IMAGEH (expected astrometry.net .wcs sidecar format)");
        fits_close_file(fptr, &status);
        return result;
    }
    result.imageWidthPx = static_cast<int>(std::lround(imageWidth));
    result.imageHeightPx = static_cast<int>(std::lround(imageHeight));

    // Pixel scale straight from the CD matrix -- same formula as the Python
    // prototype: scale = sqrt(|det(CD)|) * 3600 arcsec/pixel. Read
    // separately from the wcslib parse below since that's the simplest way
    // to get at the raw matrix values regardless of how wcslib chooses to
    // represent them internally.
    double cd11 = 0.0, cd12 = 0.0, cd21 = 0.0, cd22 = 0.0;
    const bool haveFullCd = readNumericKey(fptr, "CD1_1", &cd11) &&
                             readNumericKey(fptr, "CD1_2", &cd12) &&
                             readNumericKey(fptr, "CD2_1", &cd21) &&
                             readNumericKey(fptr, "CD2_2", &cd22);
    if (!haveFullCd) {
        result.errorMessage = QStringLiteral(
            "WCS file is missing the CD1_1/CD1_2/CD2_1/CD2_2 matrix (expected astrometry.net "
            "output convention, not PC+CDELT)");
        fits_close_file(fptr, &status);
        return result;
    }
    const double detCd = cd11 * cd22 - cd12 * cd21;
    result.pixelScaleArcsecPerPix = std::sqrt(std::fabs(detCd)) * 3600.0;
    result.fieldWidthArcmin = imageWidth * result.pixelScaleArcsecPerPix / 60.0;
    result.fieldHeightArcmin = imageHeight * result.pixelScaleArcsecPerPix / 60.0;
    result.cd11 = cd11;
    result.cd12 = cd12;
    result.cd21 = cd21;
    result.cd22 = cd22;

    double crval1 = 0.0, crval2 = 0.0, crpix1 = 0.0, crpix2 = 0.0;
    const bool haveLinearRef = readNumericKey(fptr, "CRVAL1", &crval1) &&
                                readNumericKey(fptr, "CRVAL2", &crval2) &&
                                readNumericKey(fptr, "CRPIX1", &crpix1) &&
                                readNumericKey(fptr, "CRPIX2", &crpix2);
    if (!haveLinearRef) {
        result.errorMessage = QStringLiteral("WCS file is missing CRVAL1/CRVAL2/CRPIX1/CRPIX2");
        fits_close_file(fptr, &status);
        return result;
    }
    result.crval1Deg = crval1;
    result.crval2Deg = crval2;
    result.crpix1 = crpix1;
    result.crpix2 = crpix2;

    // Now the actual pixel->world conversion, via wcslib, so that a TAN-SIP
    // header (SIP distortion terms included) is honored rather than just
    // trusting the linear CD matrix for the field center too.
    char *header = nullptr;
    int nkeys = 0;
    if (fits_hdr2str(fptr, 0, nullptr, 0, &header, &nkeys, &status) != 0) {
        result.errorMessage =
            QStringLiteral("failed to extract FITS header: %1").arg(cfitsioErrorText(status));
        fits_close_file(fptr, &status);
        return result;
    }
    fits_close_file(fptr, &status);

    int nreject = 0;
    int nwcs = 0;
    wcsprm *wcsHead = nullptr;
    const int pihStatus =
        wcspih(header, nkeys, WCSHDR_all, 0, &nreject, &nwcs, &wcsHead);
    // header was allocated by cfitsio; wcspih() copies what it needs.
    std::free(header);

    if (pihStatus != 0 || nwcs < 1 || wcsHead == nullptr) {
        result.errorMessage =
            QStringLiteral("wcslib failed to parse the WCS header (wcspih status %1)")
                .arg(pihStatus);
        if (wcsHead)
            wcsvfree(&nwcs, &wcsHead);
        return result;
    }

    wcsprm *wcs = &wcsHead[0];
    const int setStatus = wcsset(wcs);
    if (setStatus != 0) {
        result.errorMessage =
            QStringLiteral("wcslib wcsset() failed (status %1)").arg(setStatus);
        wcsvfree(&nwcs, &wcsHead);
        return result;
    }

    // Field-center pixel: FITS pixel coordinates are 1-indexed with pixel
    // centers at integers, so the geometric center of an imageWidth x
    // imageHeight image is at ((imageWidth+1)/2, (imageHeight+1)/2).
    const double pixcrd[2] = {(imageWidth + 1.0) / 2.0, (imageHeight + 1.0) / 2.0};
    double imgcrd[2] = {0.0, 0.0};
    double phi = 0.0, theta = 0.0;
    double world[2] = {0.0, 0.0};
    int stat = 0;
    const int p2sStatus = wcsp2s(wcs, 1, 2, pixcrd, imgcrd, &phi, &theta, world, &stat);
    if (p2sStatus != 0) {
        result.errorMessage =
            QStringLiteral("wcslib wcsp2s() failed (status %1)").arg(p2sStatus);
        wcsvfree(&nwcs, &wcsHead);
        return result;
    }

    // wcs->lng / wcs->lat give the world[] index for the longitude/latitude
    // axis respectively -- populated by wcsset(), and the robust way to
    // find RA/Dec rather than just assuming index 0/1, though for
    // astrometry.net's standard RA---TAN[-SIP]/DEC--TAN[-SIP] output that's
    // exactly what it works out to.
    result.centerRaDeg = world[wcs->lng];
    result.centerDecDeg = world[wcs->lat];

    wcsvfree(&nwcs, &wcsHead);

    result.solved = true;
    return result;
}

PlateSolveResult PlateSolver::solve(const QString &imagePath, const PlateSolveOptions &options)
{
    PlateSolveResult result;

    if (!QFileInfo::exists(imagePath)) {
        result.errorMessage = QStringLiteral("image does not exist: %1").arg(imagePath);
        return result;
    }

    const QFileInfo imageInfo(imagePath);
    const QString baseName = imageInfo.completeBaseName();
    const QDir dir = imageInfo.dir();
    const QString wcsPath = dir.filePath(baseName + ".wcs");
    const QString solvedMarkerPath = dir.filePath(baseName + ".solved");

    QStringList args;
    args << "--no-plots" << "--overwrite";
    args << "--cpulimit" << QString::number(options.cpuLimitSeconds);
    args << "--downsample" << QString::number(options.downsample);

    if (!std::isnan(options.hintRaDeg) && !std::isnan(options.hintDecDeg)) {
        args << "--ra" << QString::number(options.hintRaDeg, 'f', 6);
        args << "--dec" << QString::number(options.hintDecDeg, 'f', 6);
        args << "--radius" << QString::number(options.hintRadiusDeg, 'f', 3);
    }
    if (!std::isnan(options.scaleLowArcsecPerPix) && !std::isnan(options.scaleHighArcsecPerPix)) {
        args << "--scale-units" << "arcsecperpix";
        args << "--scale-low" << QString::number(options.scaleLowArcsecPerPix, 'f', 4);
        args << "--scale-high" << QString::number(options.scaleHighArcsecPerPix, 'f', 4);
    }
    args << imagePath;

    QProcess proc;
    proc.setProgram(options.solveFieldPath);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        result.errorMessage =
            QStringLiteral("failed to start '%1' -- is astrometry.net installed and on PATH?")
                .arg(options.solveFieldPath);
        return result;
    }

    // Give solve-field a bit of headroom over its own --cpulimit to actually
    // exit and flush output, rather than racing it.
    const int timeoutMs = (options.cpuLimitSeconds + 30) * 1000;
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(5000);
        result.errorMessage = QStringLiteral("solve-field timed out after %1s")
                                   .arg(options.cpuLimitSeconds + 30);
        return result;
    }

    const QString combinedOutput = QString::fromLocal8Bit(proc.readAllStandardOutput()) +
                                    QString::fromLocal8Bit(proc.readAllStandardError());

    if (!QFileInfo::exists(solvedMarkerPath) || !QFileInfo::exists(wcsPath)) {
        result.errorMessage = combinedOutput.isEmpty()
                                   ? QStringLiteral("solve-field did not solve the field "
                                                     "(no .solved/.wcs output produced)")
                                   : QStringLiteral("solve-field did not solve the field:\n%1")
                                         .arg(combinedOutput.trimmed());
        return result;
    }

    return readWcsFile(wcsPath);
}

} // namespace epochfrom
