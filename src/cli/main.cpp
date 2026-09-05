#include "GaiaCatalog.h"
#include "EpochFit.h"
#include "PlateSolver.h"
#include "EquipmentCalibrator.h"
#include "EquipmentProfile.h"
#include "ImageDater.h"
#include "ReportFormatting.h"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace epochfrom;

namespace {

int runSelfTest(const QCommandLineParser &parser, QTextStream &out)
{
    const QString gaiaPath = parser.value("gaia");
    if (gaiaPath.isEmpty()) {
        out << "error: --gaia <path.csv> is required\n";
        return 1;
    }
    const double trueEpoch = parser.value("true-epoch").toDouble();
    const double noiseArcsec = parser.value("noise-arcsec").toDouble();
    const int nStars = parser.value("n-stars").toInt();
    const unsigned seed = parser.value("seed").toUInt();

    QVector<GaiaStar> catalog;
    QString err;
    if (!GaiaCatalog::loadCsv(gaiaPath, &catalog, &err)) {
        out << "error: " << err << "\n";
        return 1;
    }
    out << "Loaded " << catalog.size() << " usable stars from " << gaiaPath << "\n";

    const EpochFit::Result result =
        EpochFit::runSelfTest(catalog, trueEpoch, noiseArcsec, seed, nStars);

    out << "\n--- Self-test ---\n";
    out << "Stars used:        " << result.nStarsUsed << " (median match sep "
        << QString::number(result.medianMatchSepArcsec, 'f', 3) << "\")\n";
    out << "Simulated noise:   " << QString::number(noiseArcsec, 'f', 2) << "\" per star\n";
    out << "True epoch:        " << QString::number(trueEpoch, 'f', 4) << "  ("
        << jyearToDateString(trueEpoch) << ")\n";
    out << "Fitted epoch:      " << QString::number(result.epochJyear, 'f', 4) << "  ("
        << jyearToDateString(result.epochJyear) << ")  +/- "
        << QString::number(result.epochSigmaYears, 'f', 4) << " yr\n";
    out << "Error:             " << QString::number((result.epochJyear - trueEpoch) * 365.25, 'f', 1)
        << " days\n";
    out << "RMS residual:      " << QString::number(result.rmsResidualMas, 'f', 1) << " mas\n";
    out << "Fitted zero-point offset: RA " << QString::number(result.raOffsetMas, 'f', 1)
        << " mas, Dec " << QString::number(result.decOffsetMas, 'f', 1)
        << " mas (should be ~0 -- synthetic test, no real WCS error)\n";
    if (result.rankDeficient)
        out << "WARNING: fit Jacobian is rank-deficient -- epoch and the RA/Dec offset "
               "aren't fully separable with this star set.\n";
    if (!result.converged)
        out << "WARNING: fit did not report convergence.\n";

    return 0;
}

PlateSolveOptions buildSolveOptions(const QCommandLineParser &parser)
{
    PlateSolveOptions options;
    if (parser.isSet("ra") && parser.isSet("dec")) {
        options.hintRaDeg = parser.value("ra").toDouble();
        options.hintDecDeg = parser.value("dec").toDouble();
        if (parser.isSet("radius"))
            options.hintRadiusDeg = parser.value("radius").toDouble();
    }
    if (parser.isSet("scale-low") && parser.isSet("scale-high")) {
        options.scaleLowArcsecPerPix = parser.value("scale-low").toDouble();
        options.scaleHighArcsecPerPix = parser.value("scale-high").toDouble();
    }
    if (parser.isSet("downsample"))
        options.downsample = parser.value("downsample").toInt();
    if (parser.isSet("cpulimit"))
        options.cpuLimitSeconds = parser.value("cpulimit").toInt();
    if (parser.isSet("solve-field-path"))
        options.solveFieldPath = parser.value("solve-field-path");
    if (parser.isSet("update-fits-header"))
        options.updateFitsHeader = true;
    return options;
}

int runSolveOne(const QString &imagePath, const PlateSolveOptions &options, bool wcsOnly,
                 QTextStream &out)
{
    const PlateSolveResult result =
        wcsOnly ? PlateSolver::readWcsFile(imagePath) : PlateSolver::solve(imagePath, options);

    if (!result.solved) {
        out << "error: " << result.errorMessage << "\n";
        return 1;
    }

    printSolveResult(result, out);

    return 0;
}

int runSolveDir(const QCommandLineParser &parser, QTextStream &out)
{
    const QDir dir(parser.value("dir"));
    if (!dir.exists()) {
        out << "error: --dir " << dir.path() << " does not exist\n";
        return 1;
    }
    const QStringList fitsFiles = dir.entryList({"*.fits", "*.fit", "*.fts"}, QDir::Files, QDir::Name);
    if (fitsFiles.isEmpty()) {
        out << "error: no .fits/.fit/.fts files found in " << dir.path() << "\n";
        return 1;
    }

    const PlateSolveOptions options = buildSolveOptions(parser);
    const bool force = parser.isSet("force");

    int solved = 0, failed = 0, skipped = 0;
    for (const QString &fitsName : fitsFiles) {
        const QFileInfo fi(dir.filePath(fitsName));
        const QString wcsPath = dir.filePath(fi.completeBaseName() + ".wcs");
        out << fitsName << ": ";
        out.flush();
        if (!force && QFileInfo::exists(wcsPath)) {
            out << "already solved, skipping (pass --force to re-solve)\n";
            ++skipped;
            continue;
        }
        const PlateSolveResult result = PlateSolver::solve(fi.filePath(), options);
        if (!result.solved) {
            out << "FAILED -- " << result.errorMessage.section('\n', 0, 0) << "\n";
            ++failed;
            continue;
        }
        out << QString("RA %1  Dec %2  field %3 x %4 arcmin  scale %5\"/px\n")
                   .arg(result.centerRaDeg, 0, 'f', 4)
                   .arg(result.centerDecDeg, 0, 'f', 4)
                   .arg(result.fieldWidthArcmin, 0, 'f', 1)
                   .arg(result.fieldHeightArcmin, 0, 'f', 1)
                   .arg(result.pixelScaleArcsecPerPix, 0, 'f', 3);
        if (!result.fitsHeaderUpdateWarning.isEmpty())
            out << "  warning: " << result.fitsHeaderUpdateWarning << "\n";
        ++solved;
    }

    out << QString("\n%1 solved, %2 already solved (skipped), %3 failed, out of %4 file(s)\n")
               .arg(solved)
               .arg(skipped)
               .arg(failed)
               .arg(fitsFiles.size());
    return (solved + skipped) > 0 ? 0 : 1;
}

int runSolve(const QCommandLineParser &parser, QTextStream &out)
{
    if (parser.isSet("dir"))
        return runSolveDir(parser, out);

    const QStringList args = parser.positionalArguments();
    if (args.size() < 2) {
        out << "error: usage: EpochFrom solve <image> [options], or EpochFrom solve --dir "
               "<directory>\n";
        return 1;
    }
    return runSolveOne(args.at(1), buildSolveOptions(parser), parser.isSet("wcs-only"), out);
}

int runCalibrate(const QCommandLineParser &parser, QTextStream &out)
{
    const QString gaiaPath = parser.value("gaia");
    const QString outPath = parser.value("outprofile");
    if (gaiaPath.isEmpty() || outPath.isEmpty()) {
        out << "error: --gaia <path.csv> and --outprofile <profile.json> are required\n\n";
        out << parser.helpText();
        return 1;
    }
    if (!outPath.endsWith(".json", Qt::CaseInsensitive)) {
        out << "note: --outprofile " << outPath << " doesn't end in .json -- the file will be "
               "saved as JSON regardless, but a .json extension is recommended so it's clear "
               "what it is later (this is the file you pass to `date --profile`).\n";
    }

    QVector<CalibrationSub> subs;
    if (parser.isSet("dir")) {
        const QDir dir(parser.value("dir"));
        if (!dir.exists()) {
            out << "error: --dir " << dir.path() << " does not exist\n";
            return 1;
        }
        const QStringList fitsFiles = dir.entryList({"*.fits", "*.fit", "*.fts"}, QDir::Files, QDir::Name);
        const PlateSolveOptions solveOptions = buildSolveOptions(parser);
        for (const QString &fitsName : fitsFiles) {
            const QFileInfo fi(dir.filePath(fitsName));
            const QString wcsPath = dir.filePath(fi.completeBaseName() + ".wcs");
            if (!QFileInfo::exists(wcsPath)) {
                out << fitsName << ": not yet solved, solving now...\n";
                out.flush();
                const PlateSolveResult solveResult = PlateSolver::solve(fi.filePath(), solveOptions);
                if (!solveResult.solved) {
                    out << "warning: skipping " << fitsName << " -- solve failed: "
                        << solveResult.errorMessage.section('\n', 0, 0) << "\n";
                    continue;
                }
                subs.push_back({fi.filePath(), solveResult.wcsFilePath});
                continue;
            }
            subs.push_back({fi.filePath(), wcsPath});
        }
        if (subs.isEmpty()) {
            out << "error: no <name>.fits + <name>.wcs pairs found in " << dir.path() << "\n";
            return 1;
        }
    } else {
        const QStringList images = parser.values("sub");
        const QStringList wcsFiles = parser.values("wcs");
        if (images.isEmpty()) {
            out << "error: pass --dir <dir>, or one or more --sub <image> --wcs <image.wcs> "
                   "pairs\n\n";
            out << parser.helpText();
            return 1;
        }
        if (images.size() != wcsFiles.size()) {
            out << "error: got " << images.size() << " --sub option(s) but " << wcsFiles.size()
                << " --wcs option(s) -- need exactly one --wcs per --sub, in the same order\n";
            return 1;
        }
        for (int i = 0; i < images.size(); ++i)
            subs.push_back({images[i], wcsFiles[i]});
    }

    QVector<GaiaStar> catalog;
    QString err;
    if (!GaiaCatalog::loadCsv(gaiaPath, &catalog, &err)) {
        out << "error: " << err << "\n";
        return 1;
    }
    out << "Loaded " << catalog.size() << " usable stars from " << gaiaPath << "\n";
    out << "Calibrating against " << subs.size() << " sub(s)...\n";
    out.flush();

    EquipmentCalibrationOptions options;
    if (parser.isSet("fwhm"))
        options.detection.fwhmPx = parser.value("fwhm").toDouble();
    if (parser.isSet("threshold-sigma"))
        options.detection.thresholdSigma = parser.value("threshold-sigma").toDouble();
    if (parser.isSet("match-arcsec"))
        options.matchToleranceArcsec = parser.value("match-arcsec").toDouble();
    if (parser.isSet("pixel-scale-norm"))
        options.pixelScaleNorm = parser.value("pixel-scale-norm").toDouble();
    if (parser.isSet("max-order")) {
        const int maxOrder = parser.value("max-order").toInt();
        QVector<int> orders;
        for (int o = 1; o <= maxOrder; ++o)
            orders.push_back(o);
        options.candidateOrders = orders;
    }
    if (parser.isSet("no-per-sub-affine"))
        options.fitPerSubAffine = false;

    const EquipmentCalibrationResult result = EquipmentCalibrator::calibrate(subs, catalog, options);
    if (!result.ok) {
        out << "error: " << result.errorMessage << "\n";
        return 1;
    }

    printCalibrateResult(result, subs.size(), out);

    EquipmentProfile profile = result.profile;
    profile.label = parser.value("label");
    profile.telescopeApertureMm = parser.value("aperture-mm").toDouble();
    profile.focalLengthMm = parser.value("focal-length-mm").toDouble();
    profile.correctorType = parser.isSet("corrector-type") ? parser.value("corrector-type") : QStringLiteral("unknown");
    profile.cameraModel = parser.value("camera");
    profile.pixelSizeUm = parser.value("pixel-size-um").toDouble();
    profile.calibrationFilter = parser.value("filter");
    profile.validFrom = parser.value("valid-from");
    profile.validTo = parser.value("valid-to");
    profile.referenceCatalogDescription = QStringLiteral("%1 (%2 stars)").arg(gaiaPath).arg(catalog.size());

    if (profile.correctorType == "refractive") {
        profile.chromaticCorrectorWarningShown = true;
        out << "\nNOTE: corrector type is 'refractive' -- a glass corrector/reducer/Barlow "
               "typically adds chromatic blur in broadband light. If this calibration session "
               "wasn't shot through a narrowband filter, a narrowband session (if you have one) "
               "will likely calibrate more precisely -- see docs/equipment-profiling-spec.md "
               "section 7.\n";
    }

    QString saveErr;
    if (!EquipmentProfile::saveToFile(profile, outPath, &saveErr)) {
        out << "\nerror: failed to save profile: " << saveErr << "\n";
        return 1;
    }
    out << "\nSaved equipment profile to " << outPath << "\n";

    if (parser.isSet("residuals-csv")) {
        const QString csvPath = parser.value("residuals-csv");
        if (!csvPath.endsWith(".csv", Qt::CaseInsensitive)) {
            out << "note: --residuals-csv " << csvPath << " doesn't end in .csv -- the file will "
                   "still be written as CSV, but a .csv extension is recommended so it's "
                   "recognized by spreadsheet apps and by tools/residual-field.html's file "
                   "picker.\n";
        }
        QFile csvFile(csvPath);
        if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            out << "error: failed to open " << csvPath << " for writing\n";
            return 1;
        }
        QTextStream csvOut(&csvFile);
        csvOut << "sub_index,pixel_u,pixel_v,radius_px,dxi_before_mas,deta_before_mas,"
                  "dxi_after_mas,deta_after_mas,kept\n";
        for (const ResidualObservation &ro : result.observations) {
            csvOut << ro.subIndex << ',' << QString::number(ro.pixelUOffset, 'f', 3) << ','
                   << QString::number(ro.pixelVOffset, 'f', 3) << ','
                   << QString::number(ro.radiusPx, 'f', 3) << ','
                   << QString::number(ro.dxiBeforeMas, 'f', 3) << ','
                   << QString::number(ro.detaBeforeMas, 'f', 3) << ','
                   << QString::number(ro.dxiAfterMas, 'f', 3) << ','
                   << QString::number(ro.detaAfterMas, 'f', 3) << ',' << (ro.keptInFit ? 1 : 0)
                   << '\n';
        }
        csvFile.close();
        out << "Saved per-observation residuals (" << result.observations.size() << " rows) to "
            << csvPath << "\n";
        out << "Open tools/residual-field.html in a browser and load this CSV to visualize it "
               "(radial vs. tangential pattern, per-axis/per-sub breakdown, before/after).\n";
    }

    return 0;
}

DateEstimateOptions buildDateOptions(const QCommandLineParser &parser, const EquipmentProfile *profile)
{
    DateEstimateOptions options;
    if (parser.isSet("fwhm"))
        options.detection.fwhmPx = parser.value("fwhm").toDouble();
    if (parser.isSet("threshold-sigma"))
        options.detection.thresholdSigma = parser.value("threshold-sigma").toDouble();
    if (parser.isSet("match-arcsec"))
        options.maxMatchArcsec = parser.value("match-arcsec").toDouble();
    if (parser.isSet("t0-guess"))
        options.t0GuessJyear = parser.value("t0-guess").toDouble();
    if (parser.isSet("obs-sigma-mas"))
        options.obsSigmaMas = parser.value("obs-sigma-mas").toDouble();
    options.equipmentProfile = profile;
    return options;
}

int runDateOne(const QString &imagePath, const QString &wcsPath, const QVector<GaiaStar> &catalog,
               const DateEstimateOptions &options, QTextStream &out)
{
    const DateEstimateResult result = ImageDater::estimate(imagePath, wcsPath, catalog, options);
    if (!result.ok) {
        out << "error: " << result.errorMessage << "\n";
        return 1;
    }
    printDateResult(result, out);
    return 0;
}

int runDateDir(const QCommandLineParser &parser, const QVector<GaiaStar> &catalog,
               const DateEstimateOptions &baseOptions, QTextStream &out)
{
    const QDir dir(parser.value("dir"));
    if (!dir.exists()) {
        out << "error: --dir " << dir.path() << " does not exist\n";
        return 1;
    }
    const QStringList fitsFiles = dir.entryList({"*.fits", "*.fit", "*.fts"}, QDir::Files, QDir::Name);
    if (fitsFiles.isEmpty()) {
        out << "error: no .fits/.fit/.fts files found in " << dir.path() << "\n";
        return 1;
    }

    const PlateSolveOptions solveOptions = buildSolveOptions(parser);

    int dated = 0, failed = 0;
    for (const QString &fitsName : fitsFiles) {
        const QFileInfo fi(dir.filePath(fitsName));
        QString wcsPath = dir.filePath(fi.completeBaseName() + ".wcs");
        out << fitsName << ": ";
        out.flush();
        if (!QFileInfo::exists(wcsPath)) {
            const PlateSolveResult solveResult = PlateSolver::solve(fi.filePath(), solveOptions);
            if (!solveResult.solved) {
                out << "FAILED -- solve failed: " << solveResult.errorMessage.section('\n', 0, 0)
                    << "\n";
                ++failed;
                continue;
            }
            wcsPath = solveResult.wcsFilePath;
        }

        const DateEstimateResult result = ImageDater::estimate(fi.filePath(), wcsPath, catalog, baseOptions);
        if (!result.ok) {
            out << "FAILED -- " << result.errorMessage << "\n";
            ++failed;
            continue;
        }
        out << QString("%1  (epoch %2 +/- %3 yr, %4 stars, rms %5 mas)%6\n")
                   .arg(jyearToDateString(result.epochJyear))
                   .arg(result.epochJyear, 0, 'f', 4)
                   .arg(result.epochSigmaYears, 0, 'f', 4)
                   .arg(result.nStarsUsed)
                   .arg(result.rmsResidualMas, 0, 'f', 1)
                   .arg(!result.converged ? QStringLiteral("  [NOT CONVERGED]")
                                            : result.rankDeficient ? QStringLiteral("  [RANK DEFICIENT]")
                                                                    : QString());
        if (!result.profileValidityWarning.isEmpty())
            out << "  WARNING: " << result.profileValidityWarning << "\n";
        ++dated;
    }

    out << QString("\n%1 dated, %2 failed, out of %3 file(s)\n")
               .arg(dated)
               .arg(failed)
               .arg(fitsFiles.size());
    return dated > 0 ? 0 : 1;
}

int runDate(const QCommandLineParser &parser, QTextStream &out)
{
    const QString gaiaPath = parser.value("gaia");
    if (gaiaPath.isEmpty()) {
        out << "error: --gaia <path.csv> is required\n";
        return 1;
    }

    QVector<GaiaStar> catalog;
    QString err;
    if (!GaiaCatalog::loadCsv(gaiaPath, &catalog, &err)) {
        out << "error: " << err << "\n";
        return 1;
    }
    out << "Loaded " << catalog.size() << " usable stars from " << gaiaPath << "\n\n";

    EquipmentProfile profile;
    bool haveProfile = false;
    if (parser.isSet("profile")) {
        QString profileErr;
        if (!EquipmentProfile::loadFromFile(parser.value("profile"), &profile, &profileErr)) {
            out << "error: failed to load equipment profile: " << profileErr << "\n";
            return 1;
        }
        haveProfile = true;
        out << "Using equipment profile: "
            << (profile.label.isEmpty() ? parser.value("profile") : profile.label) << " (order "
            << profile.polyOrderChosen << ", held-out RMS "
            << QString::number(profile.rmsAfterHeldoutMas, 'f', 1) << " mas)\n\n";
    } else if (!parser.isSet("noprofile")) {
        out << "error: --profile <profile.json> (from `calibrate`) is required -- an "
               "uncorrected rig's own optical distortion can swamp the proper-motion signal "
               "this fit depends on, so dating without one is opt-in: pass --noprofile to "
               "date against the platesolver's own uncorrected WCS instead.\n";
        return 1;
    } else {
        out << "Dating without an equipment profile (--noprofile): using each image's raw, "
               "uncorrected platesolver WCS. Estimated dates can be off by years if this rig "
               "has any real uncorrected optical distortion -- see `calibrate --help`.\n\n";
    }

    const DateEstimateOptions options = buildDateOptions(parser, haveProfile ? &profile : nullptr);

    if (parser.isSet("dir"))
        return runDateDir(parser, catalog, options, out);

    const QStringList args = parser.positionalArguments();
    if (args.size() < 2) {
        out << "error: usage: EpochFrom date <image> [options], or EpochFrom date --dir "
               "<directory>\n";
        return 1;
    }
    const QString imagePath = args.at(1);
    const QFileInfo imageInfo(imagePath);
    QString wcsPath = parser.isSet("wcs") ? parser.value("wcs")
                                            : imageInfo.dir().filePath(imageInfo.completeBaseName() + ".wcs");
    if (!QFileInfo::exists(wcsPath)) {
        out << imageInfo.fileName() << ": not yet solved, solving now...\n";
        out.flush();
        const PlateSolveResult solveResult = PlateSolver::solve(imagePath, buildSolveOptions(parser));
        if (!solveResult.solved) {
            out << "error: solve failed: " << solveResult.errorMessage << "\n";
            return 1;
        }
        wcsPath = solveResult.wcsFilePath;
    }
    return runDateOne(imagePath, wcsPath, catalog, options, out);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("EpochFrom");
    QCoreApplication::setApplicationVersion("0.1.0");

    QTextStream out(stdout);

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "Determine the capture date of astrophotography images by fitting Gaia DR3 proper "
        "motions against plate-solved star positions.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("command", "Command to run: selftest, solve, calibrate, date", "<command>");

    // Parse loosely first just to find the subcommand, then add the right
    // options and re-parse -- QCommandLineParser doesn't have first-class
    // git-style subcommand support, this is the standard workaround.
    parser.parse(QCoreApplication::arguments());
    const QStringList args = parser.positionalArguments();
    const QString command = args.isEmpty() ? QString() : args.first();

    if (command == "selftest") {
        parser.clearPositionalArguments();
        parser.addPositionalArgument("selftest", "Run the synthetic epoch-recovery self-test");
        parser.addOption({"gaia", "Gaia catalog CSV (from gaia_field_query.py)", "path"});
        parser.addOption({"true-epoch", "Epoch to simulate, Julian year", "jyear", "2018.69"});
        parser.addOption({"noise-arcsec", "Simulated centroiding noise, arcsec", "arcsec", "0.3"});
        parser.addOption({"n-stars", "Subsample to this many stars (0 = all)", "n", "0"});
        parser.addOption({"seed", "RNG seed", "seed", "42"});
        parser.process(app);
        return runSelfTest(parser, out);
    }

    if (command == "solve") {
        parser.clearPositionalArguments();
        parser.addPositionalArgument("solve", "Plate-solve an image and print RA/Dec/field size");
        parser.addPositionalArgument(
            "image", "Path to the image (or a .wcs file with --wcs-only); omit if using --dir",
            "[image]");
        parser.addOption({"ra", "Pointing hint: RA, degrees (needs --dec too)", "deg"});
        parser.addOption({"dec", "Pointing hint: Dec, degrees (needs --ra too)", "deg"});
        parser.addOption({"radius", "Pointing hint search radius, degrees", "deg", "1.0"});
        parser.addOption({"scale-low", "Lower pixel-scale bound, arcsec/px (needs --scale-high too)",
                           "arcsec"});
        parser.addOption({"scale-high", "Upper pixel-scale bound, arcsec/px (needs --scale-low too)",
                           "arcsec"});
        parser.addOption({"downsample", "solve-field downsample factor", "n", "2"});
        parser.addOption({"cpulimit", "solve-field CPU time limit, seconds", "sec", "55"});
        parser.addOption({"solve-field-path", "Path to the solve-field binary", "path",
                           "solve-field"});
        parser.addOption({"wcs-only", "Skip solving; parse `image` as an existing .wcs file"});
        parser.addOption({"dir",
                           "Batch-solve every .fits/.fit/.fts file in this directory instead of a "
                           "single <image>; already-solved files (with a matching .wcs) are skipped",
                           "dir"});
        parser.addOption(
            {"force", "With --dir, re-solve files that already have a matching .wcs sidecar"});
        parser.addOption({"update-fits-header",
                           "After solving, also write the WCS (CRVAL/CRPIX/CD, SIP terms, and "
                           "convenience decimal RA/DEC keys) into the image's own FITS header, "
                           "in place -- not just the .wcs sidecar. Modifies the original file; "
                           "off by default"});
        parser.process(app);
        return runSolve(parser, out);
    }

    if (command == "calibrate") {
        parser.clearPositionalArguments();
        parser.addPositionalArgument("calibrate", "Fit an equipment distortion profile against Gaia");
        parser.addOption({"dir", "Directory of <name>.fits + <name>.wcs pairs to calibrate from",
                           "dir"});
        parser.addOption({"sub", "A calibration light frame (repeatable; pair with --wcs, same order)",
                           "image"});
        parser.addOption({"wcs", "That sub's solved .wcs sidecar (repeatable, same order as --sub)",
                           "path"});
        parser.addOption({"gaia", "Gaia catalog CSV covering the field (from gaia_field_query.py)",
                           "path"});
        parser.addOption({"outprofile",
                           "Where to save the fitted equipment profile, as JSON -- give it a .json "
                           "filename (this is the file `date --profile` reads)",
                           "path"});
        parser.addOption({"fwhm", "Expected stellar FWHM, pixels", "px", "4.0"});
        parser.addOption({"threshold-sigma", "Detection threshold, multiples of background noise",
                           "sigma", "6.0"});
        parser.addOption({"match-arcsec", "Cross-match tolerance to Gaia", "arcsec", "3.0"});
        parser.addOption({"max-order", "Highest polynomial order to consider", "n", "6"});
        parser.addOption({"no-per-sub-affine",
                           "Don't fit/remove each sub's own rotation+scale+shear before the shared "
                           "fit -- pools raw per-sub residuals directly instead (the old behavior)"});
        parser.addOption({"pixel-scale-norm",
                           "Polynomial term normalization, px (default: half the sensor's long axis)",
                           "px"});
        parser.addOption({"label", "Profile label, e.g. \"800mm reflector + Paracorr, ASI1600MM\"",
                           "text"});
        parser.addOption({"aperture-mm", "Telescope aperture, mm", "mm", "0"});
        parser.addOption({"focal-length-mm", "Nominal focal length, mm", "mm", "0"});
        parser.addOption({"corrector-type", "none | refractive | reflective | catadioptric", "type"});
        parser.addOption({"camera", "Camera model", "text"});
        parser.addOption({"pixel-size-um", "Camera pixel size, microns", "um", "0"});
        parser.addOption({"filter", "Filter used for this calibration session", "text"});
        parser.addOption({"valid-from", "Date this profile becomes valid, yyyy-MM-dd", "date"});
        parser.addOption({"valid-to", "Date this profile stops being valid, yyyy-MM-dd (blank = still current)",
                           "date"});
        parser.addOption({"residuals-csv",
                           "Also save a CSV of every pooled star observation (pixel position, "
                           "radius from crpix, before/after xi/eta residual, sub index) for "
                           "spatial/per-axis inspection -- give it a .csv filename, then open "
                           "tools/residual-field.html in a browser and load it to visualize",
                           "path"});
        parser.addOption({"ra", "Solve hint for any unsolved subs: RA, degrees (needs --dec too)", "deg"});
        parser.addOption({"dec", "Solve hint for any unsolved subs: Dec, degrees (needs --ra too)", "deg"});
        parser.addOption({"radius", "Solve hint search radius, degrees", "deg", "1.0"});
        parser.addOption({"scale-low",
                           "Lower pixel-scale bound for solving, arcsec/px (needs --scale-high too)",
                           "arcsec"});
        parser.addOption({"scale-high",
                           "Upper pixel-scale bound for solving, arcsec/px (needs --scale-low too)",
                           "arcsec"});
        parser.addOption({"downsample", "solve-field downsample factor", "n", "2"});
        parser.addOption({"cpulimit", "solve-field CPU time limit, seconds", "sec", "55"});
        parser.addOption({"solve-field-path", "Path to the solve-field binary", "path",
                           "solve-field"});
        parser.addOption({"update-fits-header",
                           "When a sub needs solving, also write the WCS into that sub's own "
                           "FITS header, in place -- not just its .wcs sidecar. Modifies the "
                           "original file; off by default"});
        parser.process(app);
        return runCalibrate(parser, out);
    }

    if (command == "date") {
        parser.clearPositionalArguments();
        parser.addPositionalArgument("date",
                                       "Estimate an image's capture date by fitting Gaia proper motions");
        parser.addPositionalArgument("image", "Path to the image; omit if using --dir", "[image]");
        parser.addOption({"wcs", "The image's solved .wcs sidecar (default: <image>.wcs next to it)",
                           "path"});
        parser.addOption({"dir",
                           "Batch-date every .fits/.fit/.fts file in this directory instead of a "
                           "single <image>",
                           "dir"});
        parser.addOption({"gaia", "Gaia catalog CSV covering the field (from gaia_field_query.py)",
                           "path"});
        parser.addOption({"profile",
                           "Equipment profile JSON (from `calibrate`) to apply instead of trusting "
                           "the platesolver's own SIP fit -- required unless --noprofile is given",
                           "path"});
        parser.addOption({"noprofile",
                           "Date without an equipment profile, trusting the platesolver's own "
                           "uncorrected WCS -- not recommended, but explicit opt-in since skipping "
                           "--profile silently gave wildly wrong dates before this flag existed"});
        parser.addOption({"fwhm", "Expected stellar FWHM, pixels", "px", "4.0"});
        parser.addOption({"threshold-sigma", "Detection threshold, multiples of background noise",
                           "sigma", "6.0"});
        parser.addOption({"match-arcsec", "Cross-match tolerance to Gaia", "arcsec", "3.0"});
        parser.addOption({"t0-guess", "Initial epoch guess for the fit, Julian year", "jyear", "2015.0"});
        parser.addOption({"obs-sigma-mas",
                           "Assumed per-star precision, mas (default: auto -- the equipment "
                           "profile's own held-out RMS if one is given, else 300)",
                           "mas"});
        parser.addOption({"ra", "Solve hint for any unsolved image(s): RA, degrees (needs --dec too)",
                           "deg"});
        parser.addOption({"dec", "Solve hint for any unsolved image(s): Dec, degrees (needs --ra too)",
                           "deg"});
        parser.addOption({"radius", "Solve hint search radius, degrees", "deg", "1.0"});
        parser.addOption({"scale-low",
                           "Lower pixel-scale bound for solving, arcsec/px (needs --scale-high too)",
                           "arcsec"});
        parser.addOption({"scale-high",
                           "Upper pixel-scale bound for solving, arcsec/px (needs --scale-low too)",
                           "arcsec"});
        parser.addOption({"downsample", "solve-field downsample factor", "n", "2"});
        parser.addOption({"cpulimit", "solve-field CPU time limit, seconds", "sec", "55"});
        parser.addOption({"solve-field-path", "Path to the solve-field binary", "path",
                           "solve-field"});
        parser.addOption({"update-fits-header",
                           "When an image needs solving, also write the WCS into its own FITS "
                           "header, in place -- not just its .wcs sidecar. Modifies the original "
                           "file; off by default"});
        parser.process(app);
        return runDate(parser, out);
    }

    parser.process(app);
    out << "Usage: EpochFrom <command>\n\nCommands:\n  selftest   "
           "Run the synthetic epoch-recovery self-test against a real Gaia catalog\n  solve      "
           "Plate-solve an image (or read an existing .wcs) and print RA/Dec/field size\n  "
           "calibrate  Fit a per-rig optical distortion profile against Gaia\n  date       "
           "Estimate an image's (or directory's) capture date against Gaia\n";
    return command.isEmpty() ? 1 : 1;
}
