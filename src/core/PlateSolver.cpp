#include "PlateSolver.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <fitsio.h>
#include <wcs.h>
#include <wcshdr.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

#if defined(Q_OS_UNIX)
#include <csignal>
#include <sys/types.h>
#endif

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

// Core TAN WCS keywords copied verbatim (as raw header cards, so formatting
// and comments survive untouched) from a .wcs sidecar into the original
// image's own header when PlateSolveOptions::updateFitsHeader is set.
// Deliberately excludes OBJCTRA/OBJCTDEC: those are the mount/capture
// software's own record of where it was *asked* to point, which is
// legitimate metadata in its own right (if sometimes stale, per
// get_center_from_fits() in scripts/gaia_field_query.py) and not this
// function's business to overwrite.
const char *const kCoreWcsKeys[] = {
    "WCSAXES", "CTYPE1", "CTYPE2", "CUNIT1", "CUNIT2",
    "CRVAL1",  "CRVAL2", "CRPIX1", "CRPIX2",
    "CD1_1",   "CD1_2",  "CD2_1",  "CD2_2",
    "EQUINOX", "LONPOLE", "LATPOLE", "RADESYS", "RADESYSA",
};

// SIP distortion polynomial families: each is present only if solve-field
// found it worth fitting one, so every family is optional and independently
// checked via its own "*_ORDER" card.
struct SipFamily {
    const char *orderKey;
    const char *prefix;
};
const SipFamily kSipFamilies[] = {
    {"A_ORDER", "A_"},
    {"B_ORDER", "B_"},
    {"AP_ORDER", "AP_"},
    {"BP_ORDER", "BP_"},
};

// Copies one header card verbatim from src to dst, if present in src.
// Absence in src is not an error -- callers use this to opportunistically
// pull whichever of a fixed keyword list actually exist.
void copyCardIfPresent(fitsfile *src, fitsfile *dst, const char *key, int *status)
{
    char card[FLEN_CARD];
    int localStatus = 0;
    if (fits_read_card(src, key, card, &localStatus) != 0)
        return;
    fits_update_card(dst, key, card, status);
}

} // namespace

// Copies the WCS solution (core TAN keywords, any SIP terms, plus
// convenience decimal RA/DEC keys) from a freshly-solved .wcs sidecar into
// the original image's own FITS header, in place. This is a deliberate,
// opt-in exception to this project's usual separation of concerns (pixel
// data + DATE-OBS in the FITS file, linear WCS terms in the .wcs sidecar --
// see the CalibrationSub comment in EquipmentCalibrator.h) for users who
// want their light frames self-describing, e.g. for other tools that don't
// know to look for a sidecar. Returns an empty string on success, or a
// human-readable warning on failure; failure here never implies the solve
// itself failed -- the .wcs sidecar is already written and valid
// independently of this step.
QString PlateSolver::writeWcsIntoFits(const QString &imagePath, const QString &wcsPath,
                                       const PlateSolveResult &result)
{
    int status = 0;
    fitsfile *wcsFptr = nullptr;
    if (fits_open_file(&wcsFptr, wcsPath.toLocal8Bit().constData(), READONLY, &status) != 0) {
        return QStringLiteral("could not reopen %1 to update the image's FITS header: %2")
            .arg(wcsPath, cfitsioErrorText(status));
    }

    fitsfile *imgFptr = nullptr;
    if (fits_open_file(&imgFptr, imagePath.toLocal8Bit().constData(), READWRITE, &status) != 0) {
        const QString warning = QStringLiteral("could not open %1 for writing to update its FITS "
                                                "header: %2")
                                     .arg(imagePath, cfitsioErrorText(status));
        int closeStatus = 0;
        fits_close_file(wcsFptr, &closeStatus);
        return warning;
    }

    for (const char *key : kCoreWcsKeys)
        copyCardIfPresent(wcsFptr, imgFptr, key, &status);

    for (const SipFamily &family : kSipFamilies) {
        char orderCard[FLEN_CARD];
        int probeStatus = 0;
        if (fits_read_card(wcsFptr, family.orderKey, orderCard, &probeStatus) != 0)
            continue; // this SIP family isn't in the .wcs file -- not every solve has SIP

        long order = 0;
        probeStatus = 0;
        if (fits_read_key(wcsFptr, TLONG, family.orderKey, &order, nullptr, &probeStatus) != 0)
            continue;

        fits_update_card(imgFptr, family.orderKey, orderCard, &status);

        for (long i = 0; i <= order; ++i) {
            for (long j = 0; j <= order - i; ++j) {
                const QByteArray termKey = QByteArray(family.prefix) + QByteArray::number(i) + '_' +
                                            QByteArray::number(j);
                copyCardIfPresent(wcsFptr, imgFptr, termKey.constData(), &status);
            }
        }
    }

    int wcsCloseStatus = 0;
    fits_close_file(wcsFptr, &wcsCloseStatus);

    // Convenience decimal-degree keys, deliberately using the same "RA"/
    // "DEC" names get_center_from_fits() (scripts/gaia_field_query.py)
    // already falls back to when CRVAL1/2 aren't present -- so a light frame
    // updated this way becomes directly usable there too, not just a
    // cosmetic addition.
    double ra = result.centerRaDeg;
    double dec = result.centerDecDeg;
    fits_update_key(imgFptr, TDOUBLE, "RA", &ra, "Solved field center RA, deg (EpochFrom)", &status);
    fits_update_key(imgFptr, TDOUBLE, "DEC", &dec, "Solved field center Dec, deg (EpochFrom)",
                     &status);

    fits_write_history(imgFptr,
                        "WCS solution written into this header by EpochFrom "
                        "solve --update-fits-header",
                        &status);
    // Recomputes DATASUM/CHECKSUM to match the now-modified header, whether
    // or not either existed before.
    fits_write_chksum(imgFptr, &status);

    QString warning;
    if (status != 0) {
        warning = QStringLiteral("writing the WCS into %1's FITS header failed partway through: %2")
                      .arg(imagePath, cfitsioErrorText(status));
    }

    int imgCloseStatus = 0;
    fits_close_file(imgFptr, &imgCloseStatus);
    return warning;
}

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

    // solve-field is a multi-stage pipeline (source extraction, xylist
    // augmentation, the actual matcher) and on a timeout we need to kill all
    // of it, not just the one process we spawned directly -- QProcess::kill()
    // only signals that direct child, so a lone SIGKILL to it can orphan a
    // still-running grandchild (e.g. image2xy) that keeps burning CPU in the
    // background indefinitely. Wrap the invocation in `setsid` (present on
    // any Linux system, part of util-linux) so solve-field becomes its own
    // session/process-group leader; a timeout then sends SIGKILL to the
    // whole group (negative PID), taking every stage down with it. Falls
    // back to a plain direct-child kill if `setsid` isn't found or on a
    // non-Unix build.
#if defined(Q_OS_UNIX)
    const QString setsidPath = QStandardPaths::findExecutable("setsid");
    const bool killWholeGroup = !setsidPath.isEmpty();
#else
    const bool killWholeGroup = false;
#endif

    QProcess proc;
    if (killWholeGroup) {
        proc.setProgram(setsidPath);
        proc.setArguments(QStringList{options.solveFieldPath} + args);
    } else {
        proc.setProgram(options.solveFieldPath);
        proc.setArguments(args);
    }
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
#if defined(Q_OS_UNIX)
        if (killWholeGroup) {
            // setsid execs straight into solve-field without forking, so the
            // PID QProcess tracked for its "setsid" child is solve-field's
            // own PID post-exec -- and since setsid() makes the calling
            // process both session and process-group leader, that PID also
            // is the group's PGID. Negating it targets the whole group.
            const qint64 pid = proc.processId();
            if (pid > 0)
                ::kill(-static_cast<pid_t>(pid), SIGKILL);
        }
#endif
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

    PlateSolveResult finalResult = readWcsFile(wcsPath);
    if (finalResult.solved && options.updateFitsHeader)
        finalResult.fitsHeaderUpdateWarning = PlateSolver::writeWcsIntoFits(imagePath, wcsPath, finalResult);
    return finalResult;
}

} // namespace epochfrom
