#include "PlateSolver.h"

#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QVector>

#include <fitsio.h>
#include <wcs.h>
#include <wcshdr.h>
#include <wcserr.h>

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

// ANSVR's solve-field.exe (a common Windows source for solve-field, and the
// one this was actually confirmed against) is a Cygwin binary, and Cygwin's
// runtime DLL isn't colocated with it: on a real ANSVR install,
// solve-field.exe sits under "<ansvr>/lib/astrometry/bin/" while
// cygwin1.dll lives several levels up, under the shared "<ansvr>/bin/" every
// Cygwin-installed package uses. Confirmed as the actual, sole cause of
// solve-field.exe silently failing to launch under Windows' own error
// dialog ("The code execution cannot proceed because cygwin1.dll was not
// found") the moment it's started from anywhere other than ANSVR's own
// tray app/environment (which presumably puts that directory on PATH
// itself) -- a plain QProcess::start(), with none of that surrounding
// environment, hits it every time. Rather than hardcoding that exact
// layout, search upward from solve-field.exe's own directory for a
// cygwin1.dll sitting either right there or in a "bin" sibling.
//
// Returns both that DLL's own directory (runtimeDir, kept for its own sake --
// PATH needs it directly) and, separately, what looks like the install's
// root directory (rootDir): whichever ancestor directory turned out to
// contain (or directly own a "bin" holding) cygwin1.dll. rootDir is what
// collectDllDirectories() below scans, since it's the natural place to find
// every *other* DLL this install ships, not just cygwin1.dll's own.
// Harmless empty result for a solve-field build that isn't a Cygwin binary
// at all (nothing found nearby, caller leaves PATH alone).
struct CygwinLocation {
    QString runtimeDir;
    QString rootDir;
};

CygwinLocation findCygwinLocation(const QString &solveFieldExePath)
{
    QDir dir = QFileInfo(solveFieldExePath).dir();
    for (int level = 0; level < 6; ++level) {
        // cygwin1.dll sitting directly in `dir` means `dir` itself is acting
        // as a "bin"-style directory, so its parent is the better root guess.
        if (QFileInfo::exists(dir.filePath("cygwin1.dll"))) {
            QDir root = dir;
            const QString runtimeDir = dir.absolutePath();
            root.cdUp();
            return {runtimeDir, root.absolutePath()};
        }
        if (QFileInfo::exists(dir.filePath("bin/cygwin1.dll")))
            return {dir.absoluteFilePath("bin"), dir.absolutePath()};
        if (dir.isRoot() || !dir.cdUp())
            break;
    }
    return {};
}

// A single hardcoded "put cygwin1.dll's directory on PATH" isn't enough --
// confirmed on the same real ANSVR install as the above: numpy's compiled
// core (multiarray.dll, needed transitively by solve-field's own
// image2pnm.py preprocessing step, itself needed before any actual solving
// happens) depends in turn on cygblas-0.dll, which this install keeps under
// "lib/lapack" -- a completely different directory from cygwin1.dll's own
// "bin", with no reason to assume it's the last one either. Rather than
// keep discovering and hardcoding one more special-cased subdirectory every
// time a deeper pipeline stage needs a DLL from somewhere else in the
// install, walk the whole install tree (breadth-first, capped so an
// unexpectedly huge or deeply-nested tree can't make this pathological) and
// collect every directory that contains at least one *.dll -- covering any
// dependency anywhere in the install, present or future, the same way
// ANSVR's own launcher environment presumably already does. Cached per root
// directory, since this walks the filesystem and repeated solves (or the
// --help probe right below, which needs the identical environment) would
// otherwise redo it every time.
QStringList collectDllDirectories(const QString &rootPath)
{
    static QHash<QString, QStringList> cache;
    const auto cached = cache.constFind(rootPath);
    if (cached != cache.constEnd())
        return cached.value();

    QStringList found;
    QVector<QPair<QString, int>> pending;
    pending.append({rootPath, 0});
    const int maxDepth = 6;
    while (!pending.isEmpty()) {
        const auto [path, depth] = pending.takeLast();
        QDir dir(path);
        if (!dir.entryList(QStringList{QStringLiteral("*.dll")}, QDir::Files).isEmpty())
            found << dir.absolutePath();
        if (depth >= maxDepth)
            continue;
        const auto subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sub : subdirs)
            pending.append({dir.absoluteFilePath(sub), depth + 1});
    }
    cache.insert(rootPath, found);
    return found;
}

// Users end up with a solve-field path wrapped in literal double quotes
// surprisingly often -- Windows Explorer's own "Copy as path" puts them
// there automatically, and it's the natural thing to paste from a shell
// example (this project's own docs included) that quotes the path for a
// command line. A `QLineEdit` (or any other plain string field) has no
// reason to strip that back out, so by the time it reaches here the
// "path" can be `"C:\...\solve-field.exe"`, quote characters and all --
// confirmed as the actual cause of a real, reproducible regression: with
// quotes present, every `QFileInfo`-based check in this file (path
// existence, `.dir()`, findCygwinLocation()'s search) treats the quote
// as literal path content rather than a delimiter and never matches a
// real file or directory, even though Windows' own CreateProcess tolerates
// a quoted image name just fine -- so solve-field.exe still launches, but
// every one of *our* checks against the same string silently finds
// nothing, including the cygwin1.dll search, resurrecting that exact
// failure. Stripped once, centrally, rather than requiring the path field
// to be pasted in just so.
QString cleanExecutablePath(const QString &path)
{
    QString cleaned = path.trimmed();
    if (cleaned.size() >= 2 && cleaned.startsWith('"') && cleaned.endsWith('"'))
        cleaned = cleaned.mid(1, cleaned.size() - 2);
    return cleaned;
}

// Builds the environment any child solve-field.exe process should be
// launched with -- the system environment, plus every DLL-containing
// directory findCygwinLocation()/collectDllDirectories() can find under
// this install prepended to PATH. Every QProcess that runs solve-field.exe
// (the real solve below, and the --help probe just below this) needs this,
// not only the real solve: the probe launches solve-field.exe too, and
// would otherwise hit the exact same missing-DLL failure -- silently
// reporting every flag as "unsupported" rather than actually checking,
// since a process that never gets past its own DLL load produces no
// --help output to search either.
QProcessEnvironment solveFieldEnvironment(const QString &solveFieldPath)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const CygwinLocation location = findCygwinLocation(solveFieldPath);
    QStringList prependDirs;
    if (!location.rootDir.isEmpty())
        prependDirs = collectDllDirectories(location.rootDir);
    // Belt and braces: make sure the directory cygwin1.dll itself lives in
    // is on the list even if the tree walk above somehow missed it (e.g. a
    // maxDepth shallower than this install's actual layout).
    if (!location.runtimeDir.isEmpty() && !prependDirs.contains(location.runtimeDir))
        prependDirs.prepend(location.runtimeDir);
    if (!prependDirs.isEmpty()) {
        const QString path = env.value(QStringLiteral("PATH"));
        const QString prefix = prependDirs.join(QDir::listSeparator());
        env.insert(QStringLiteral("PATH"),
                   path.isEmpty() ? prefix : (prefix + QDir::listSeparator() + path));
    }
    return env;
}

// Not every astrometry.net build out there supports every flag we'd like
// to pass, and an unrecognized one isn't just ignored -- confirmed on a
// real ANSVR install: its bundled solve-field is a build from 2010
// (revision 16745), which predates --temp-axy entirely, and passing it
// makes solve-field refuse to run at all ("unknown option"), printing its
// own full --help text and exiting instead of ever touching the image.
// Probe for support via --help output before relying on it, rather than
// assuming either way -- this is what lets the same code path work
// against both a current astrometry.net (Linux, or a newer Windows
// build) and an old ANSVR bundle like this one. Cached per solve-field
// path so repeated solves (e.g. a directory batch, or the GUI's Solve
// button clicked more than once in a session) don't re-probe every time.
bool solveFieldSupportsFlag(const QString &solveFieldPath, const QString &flag)
{
    static QHash<QString, QString> helpTextCache;
    QString &cached = helpTextCache[solveFieldPath];
    if (cached.isEmpty()) {
        QProcess help;
        help.setProgram(solveFieldPath);
        help.setArguments(QStringList{QStringLiteral("--help")});
        help.setProcessEnvironment(solveFieldEnvironment(solveFieldPath));
        help.start();
        if (help.waitForStarted(5000) && help.waitForFinished(5000)) {
            cached = QString::fromLocal8Bit(help.readAllStandardOutput()) +
                     QString::fromLocal8Bit(help.readAllStandardError());
        }
        // A failed/empty probe still needs a non-empty sentinel so it isn't
        // retried on every single call; a single space can't match a
        // "--something" flag string, so it behaves as "nothing supported".
        if (cached.isEmpty())
            cached = QStringLiteral(" ");
    }
    return cached.contains(flag);
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

    // Diagnostic only, for now: wcslib's per-struct wcserr (function/file/line
    // plus a specific message, not just the numeric status) is disabled by
    // default and this codebase has never turned it on, so a failing
    // wcsset() below currently reports nothing more than a bare status
    // number. Enabling it costs nothing (a process-wide flag flip) and
    // wcs->err->msg is what actually explains *why* wcsset() rejected a
    // SIP-tagged header on the first real Windows build -- confirmed
    // needed since "status 5" (WCSERR_BAD_PARAM) alone doesn't say which
    // parameter.
    wcserr_enable(1);

    // Diagnostic only, for now: keep a copy of the exact raw header text
    // cfitsio produced (each card as actually written/read back on THIS
    // platform) so it can be dumped verbatim if anything below fails --
    // needed because our working theory (see windows-port.md) is that the
    // SIP failure originates from how the concrete card text is formatted
    // on Windows, not from wcslib's SIP-handling logic itself (which a
    // Linux-side reproduction using the identical keyword/value set does
    // NOT fail on), and that theory can't be confirmed or refuted without
    // seeing the actual bytes wcspih() was handed on the machine that
    // fails.
    const QByteArray headerCopy(header, nkeys * 80);

    int nreject = 0;
    int nwcs = 0;
    wcsprm *wcsHead = nullptr;
    const int pihStatus =
        wcspih(header, nkeys, WCSHDR_all, 0, &nreject, &nwcs, &wcsHead);
    // header was allocated by cfitsio; wcspih() copies what it needs.
    std::free(header);

    if (pihStatus != 0 || nwcs < 1 || wcsHead == nullptr) {
        result.errorMessage =
            QStringLiteral("wcslib failed to parse the WCS header (wcspih status %1, nreject %2)")
                .arg(pihStatus)
                .arg(nreject);
        if (wcsHead)
            wcsvfree(&nwcs, &wcsHead);
        return result;
    }

    if (nreject != 0) {
        // wcspih() silently drops any keycard it can't parse rather than
        // failing outright -- a dropped CRVAL/CD/SIP-coefficient card
        // still lets wcsset() "succeed", just with that value defaulted
        // to zero. Surfacing this even on the way to a solve is cheap and
        // rules out (or confirms) header-parsing data loss as the cause
        // of anything downstream looking wrong.
        result.errorMessage =
            QStringLiteral("wcslib wcspih() rejected %1 keycard(s) as unparseable "
                            "(nwcs %2) -- header as read:\n%3")
                .arg(nreject)
                .arg(nwcs)
                .arg(QString::fromLocal8Bit(headerCopy));
        wcsvfree(&nwcs, &wcsHead);
        return result;
    }

    wcsprm *wcs = &wcsHead[0];
    const int setStatus = wcsset(wcs);
    if (setStatus != 0) {
        result.errorMessage =
            QStringLiteral("wcslib wcsset() failed (status %1)").arg(setStatus);
        if (wcs->err && wcs->err->msg) {
            result.errorMessage += QStringLiteral(": %1 (in %2, %3:%4)")
                .arg(QString::fromLocal8Bit(wcs->err->msg))
                .arg(QString::fromLocal8Bit(wcs->err->function ? wcs->err->function : "?"))
                .arg(QString::fromLocal8Bit(wcs->err->file ? wcs->err->file : "?"))
                .arg(wcs->err->line_no);
        }
        result.errorMessage += QStringLiteral("\nheader as read:\n%1")
            .arg(QString::fromLocal8Bit(headerCopy));
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

    // See cleanExecutablePath(): used everywhere below instead of
    // options.solveFieldPath directly, so a path pasted in with
    // surrounding quotes (Explorer's "Copy as path", or any quoted shell
    // example) doesn't quietly break every path-based check in this
    // function even though the process itself would still launch fine.
    const QString solveFieldPath = cleanExecutablePath(options.solveFieldPath);

    const QFileInfo imageInfo(imagePath);
    const QString baseName = imageInfo.completeBaseName();
    const QDir dir = imageInfo.dir();
    const QString wcsPath = dir.filePath(baseName + ".wcs");
    const QString solvedMarkerPath = dir.filePath(baseName + ".solved");

    QStringList args;
    args << "--no-plots" << "--overwrite";
    args << "--cpulimit" << QString::number(options.cpuLimitSeconds);
    args << "--downsample" << QString::number(options.downsample);
    // solve-field's own defaults, left alone, litter the image's directory
    // with byproducts EpochFrom never reads: on a successful solve it writes
    // <base>.new (a full copy of the image with the WCS baked into its
    // header -- easy to mistake for a second capture), plus <base>.rdls,
    // <base>.match and <base>.corr; on every attempt, solved or not, it
    // also writes <base>.axy (its intermediate source-extraction list).
    // EpochFrom already gets everything it needs from the .wcs sidecar
    // (wcsPath below, which IS kept -- readWcsFile() reads it) and only
    // ever touches the original FITS header when the caller explicitly
    // opts into that via updateFitsHeader/writeWcsIntoFits, so none of
    // these are wanted: point --axy at a real temp file (astrometry.net's
    // own --temp-axy flag deletes it on exit; "none" isn't a valid value
    // for --axy the way it is for the others below) and disable the rest
    // outright. --temp-axy specifically isn't universally available (see
    // solveFieldSupportsFlag()) -- on a build old enough to lack it, the
    // .axy byproduct is simply left behind at <base>.axy; that's a known,
    // harmless limitation of that install, not something worth failing
    // the whole solve over the way an unconditional --temp-axy would.
    if (solveFieldSupportsFlag(solveFieldPath, QStringLiteral("--temp-axy")))
        args << "--temp-axy";
    args << "--new-fits" << "none";
    args << "--rdls" << "none";
    args << "--match" << "none";
    args << "--corr" << "none";

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
    // non-Unix build -- on Windows specifically, that fallback is a real,
    // currently-unaddressed gap (a timed-out solve-field's own children
    // can outlive it there); see docs/windows-port.md's known-limitations
    // section. A Windows-native fix exists (assign the child into a Job
    // Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE at creation time via
    // QProcess::setCreateProcessArgumentsModifier), just not implemented
    // here yet.
#if defined(Q_OS_UNIX)
    const QString setsidPath = QStandardPaths::findExecutable("setsid");
    const bool killWholeGroup = !setsidPath.isEmpty();
#else
    // Declared (empty) unconditionally, not just under Q_OS_UNIX: the
    // `if (killWholeGroup) { proc.setProgram(setsidPath); ... }` below is
    // ordinary runtime code, not preprocessor-conditional, so it still needs
    // `setsidPath` to exist as a name on every platform even though
    // killWholeGroup is always false here and that branch never actually
    // runs -- confirmed as a real "'setsidPath' was not declared in this
    // scope" error on the first real Windows/MinGW compile.
    const QString setsidPath;
    const bool killWholeGroup = false;
#endif

    // Cygwin's fork() emulation on Windows needs every DLL in a forked child
    // to land at an exact, precomputed address; even with the underlying
    // causes above fixed (this install additionally needed `rebaseall` run
    // once, outside EpochFrom's control -- see docs/windows-port.md's ANSVR
    // section), some unrelated DLL loaded at a Windows-randomized address
    // can still occasionally collide with one of those precomputed
    // addresses on a given run. Confirmed real and transient on a real
    // ANSVR install: solve-field's own image2pnm.py preprocessing step
    // aborted outright with Cygwin's own "child_info_fork::abort: address
    // space needed by '<dll>' ... is already occupied", and simply
    // retrying the exact same solve immediately afterwards, completely
    // unchanged, succeeded. Rather than surface this well-known Cygwin
    // flakiness as a hard failure and rely on the user noticing and
    // clicking Solve again by hand, detect that specific signature and
    // retry solve-field itself a bounded number of times first.
    const int maxAttempts = 3;
    QString combinedOutput;
    bool solvedMarkersExist = false;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        QProcess proc;
        if (killWholeGroup) {
            proc.setProgram(setsidPath);
            proc.setArguments(QStringList{solveFieldPath} + args);
        } else {
            proc.setProgram(solveFieldPath);
            proc.setArguments(args);
        }

        // See solveFieldEnvironment()/findCygwinLocation()/collectDllDirectories():
        // a Cygwin-built solve-field.exe (ANSVR's, confirmed) needs cygwin1.dll,
        // and every other DLL its own pipeline transitively depends on (numpy's
        // compiled core -> cygblas-0.dll, confirmed as a second, separate
        // instance of this exact problem), findable via PATH when launched
        // without its usual surrounding environment, which a plain
        // QProcess::start() doesn't provide. Setting it here, rather than
        // requiring the user to edit their system PATH by hand, is what
        // actually makes ANSVR usable from EpochFrom at all -- confirmed:
        // without this, solve-field never gets past its own DLL load (or a
        // later stage's), and both QProcess and the caller see nothing but an
        // empty stdout/stderr and a plain "did not solve" result, with no
        // indication anything failed to even start. Harmless no-op (plain
        // system environment) for a solve-field build that isn't a Cygwin
        // binary at all.
        proc.setProcessEnvironment(solveFieldEnvironment(solveFieldPath));

        proc.start();
        if (!proc.waitForStarted(10000)) {
            result.errorMessage =
                QStringLiteral("failed to start '%1' -- is astrometry.net installed and on PATH?")
                    .arg(solveFieldPath);
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

        combinedOutput = QString::fromLocal8Bit(proc.readAllStandardOutput()) +
                          QString::fromLocal8Bit(proc.readAllStandardError());
        solvedMarkersExist = QFileInfo::exists(solvedMarkerPath) && QFileInfo::exists(wcsPath);
        if (solvedMarkersExist)
            break;
        // Only this one specific, well-understood transient signature gets
        // retried automatically -- anything else (a genuine "no match
        // found", a bad path, an unsupported flag) is a real result the
        // caller should see immediately, not silently retried away.
        if (attempt >= maxAttempts || !combinedOutput.contains(QStringLiteral("child_info_fork::abort")))
            break;
    }

    if (!solvedMarkersExist) {
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
