#include "DateWorker.h"
#include "FitsDirScan.h"
#include "ReportFormatting.h"

#include <QDir>
#include <QFileInfo>
#include <QTextStream>

using namespace epochfrom;

namespace epochfrom::gui {

void DateWorker::run()
{
    QVector<GaiaStar> catalog;
    QString err;
    if (!GaiaCatalog::loadCsv(request_.gaiaCsvPath, &catalog, &err)) {
        emit logLine(QStringLiteral("error: %1\n").arg(err));
        emit finished(false);
        return;
    }
    emit logLine(QStringLiteral("Loaded %1 usable stars from %2\n\n").arg(catalog.size()).arg(request_.gaiaCsvPath));

    EquipmentProfile profile;
    bool haveProfile = false;
    if (!request_.profilePath.isEmpty()) {
        QString profileErr;
        if (!EquipmentProfile::loadFromFile(request_.profilePath, &profile, &profileErr)) {
            emit logLine(QStringLiteral("error: failed to load equipment profile: %1\n").arg(profileErr));
            emit finished(false);
            return;
        }
        haveProfile = true;
        emit logLine(QStringLiteral("Using equipment profile: %1 (order %2, held-out RMS %3 mas)\n\n")
                          .arg(profile.label.isEmpty() ? request_.profilePath : profile.label)
                          .arg(profile.polyOrderChosen)
                          .arg(QString::number(profile.rmsAfterHeldoutMas, 'f', 1)));
    } else {
        emit logLine(QStringLiteral(
            "Dating without an equipment profile: using each image's raw, uncorrected "
            "platesolver WCS. Estimated dates can be off by years if this rig has any real "
            "uncorrected optical distortion -- run a calibration first if you have one.\n\n"));
    }

    DateEstimateOptions options = request_.dateOptions;
    options.equipmentProfile = haveProfile ? &profile : nullptr;

    if (!request_.isDir) {
        const QFileInfo imageInfo(request_.path);
        QString wcsPath = !request_.wcsPath.isEmpty() ? request_.wcsPath
                                                        : imageInfo.dir().filePath(imageInfo.completeBaseName() + ".wcs");
        if (!QFileInfo::exists(wcsPath)) {
            emit logLine(QStringLiteral("%1: not yet solved, solving now...\n").arg(imageInfo.fileName()));
            const PlateSolveResult solveResult = PlateSolver::solve(request_.path, request_.solveOptions);
            if (!solveResult.solved) {
                emit logLine(QStringLiteral("error: solve failed: %1\n").arg(solveResult.errorMessage));
                emit finished(false);
                return;
            }
            wcsPath = solveResult.wcsFilePath;
        }

        const DateEstimateResult result = ImageDater::estimate(request_.path, wcsPath, catalog, options);
        if (!result.ok) {
            emit logLine(QStringLiteral("error: %1\n").arg(result.errorMessage));
            emit finished(false);
            return;
        }
        QString text;
        QTextStream out(&text);
        printDateResult(result, out);
        emit logLine(text);
        emit summaryReady(jyearToDateString(result.epochJyear), result.epochJyear,
                           result.epochSigmaYears, result.rmsResidualMas);
        emit finished(true);
        return;
    }

    const QDir dir(request_.path);
    if (!dir.exists()) {
        emit logLine(QStringLiteral("error: directory %1 does not exist\n").arg(dir.path()));
        emit finished(false);
        return;
    }
    const QStringList fitsFiles = listFitsFiles(dir);
    if (fitsFiles.isEmpty()) {
        emit logLine(QStringLiteral("error: no .fits/.fit/.fts files found in %1\n").arg(dir.path()));
        emit finished(false);
        return;
    }

    int dated = 0, failed = 0;
    for (const QString &fitsName : fitsFiles) {
        const QFileInfo fi(dir.filePath(fitsName));
        QString wcsPath = wcsSidecarPath(fi);
        if (!QFileInfo::exists(wcsPath)) {
            emit logLine(QStringLiteral("%1: not yet solved, solving now...\n").arg(fitsName));
            const PlateSolveResult solveResult = PlateSolver::solve(fi.filePath(), request_.solveOptions);
            if (!solveResult.solved) {
                emit logLine(QStringLiteral("%1: FAILED -- solve: %2\n")
                                  .arg(fitsName, solveResult.errorMessage.section('\n', 0, 0)));
                ++failed;
                continue;
            }
            wcsPath = solveResult.wcsFilePath;
        }
        const DateEstimateResult result = ImageDater::estimate(fi.filePath(), wcsPath, catalog, options);
        if (!result.ok) {
            emit logLine(QStringLiteral("%1: FAILED -- %2\n").arg(fitsName, result.errorMessage));
            ++failed;
            continue;
        }
        emit logLine(QStringLiteral("%1: estimated date %2  (epoch %3 +/- %4 yr, RMS %5 mas)\n")
                          .arg(fitsName, jyearToDateString(result.epochJyear),
                               QString::number(result.epochJyear, 'f', 4),
                               QString::number(result.epochSigmaYears, 'f', 4),
                               QString::number(result.rmsResidualMas, 'f', 1)));
        ++dated;
    }

    emit logLine(QStringLiteral("\n%1 dated, %2 failed, out of %3 file(s)\n")
                     .arg(dated)
                     .arg(failed)
                     .arg(fitsFiles.size()));
    emit finished(dated > 0);
}

} // namespace epochfrom::gui
