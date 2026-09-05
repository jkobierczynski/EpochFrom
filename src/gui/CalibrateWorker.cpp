#include "CalibrateWorker.h"
#include "FitsDirScan.h"
#include "ReportFormatting.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

using namespace epochfrom;

namespace epochfrom::gui {

void CalibrateWorker::run()
{
    const QDir dir(request_.dirPath);
    if (!dir.exists()) {
        emit logLine(QStringLiteral("error: directory %1 does not exist\n").arg(dir.path()));
        emit finished(false);
        return;
    }

    QVector<CalibrationSub> subs;
    const QStringList fitsFiles = listFitsFiles(dir);
    for (const QString &fitsName : fitsFiles) {
        const QFileInfo fi(dir.filePath(fitsName));
        const QString wcsPath = wcsSidecarPath(fi);
        if (!QFileInfo::exists(wcsPath)) {
            emit logLine(QStringLiteral("%1: not yet solved, solving now...\n").arg(fitsName));
            const PlateSolveResult solveResult = PlateSolver::solve(fi.filePath(), request_.solveOptions);
            if (!solveResult.solved) {
                emit logLine(QStringLiteral("warning: skipping %1 -- solve failed: %2\n")
                                  .arg(fitsName, solveResult.errorMessage.section('\n', 0, 0)));
                continue;
            }
            subs.push_back({fi.filePath(), solveResult.wcsFilePath});
            continue;
        }
        subs.push_back({fi.filePath(), wcsPath});
    }
    if (subs.isEmpty()) {
        emit logLine(QStringLiteral("error: no <name>.fits + <name>.wcs pairs found in %1\n").arg(dir.path()));
        emit finished(false);
        return;
    }

    QVector<GaiaStar> catalog;
    QString err;
    if (!GaiaCatalog::loadCsv(request_.gaiaCsvPath, &catalog, &err)) {
        emit logLine(QStringLiteral("error: %1\n").arg(err));
        emit finished(false);
        return;
    }
    emit logLine(QStringLiteral("Loaded %1 usable stars from %2\n").arg(catalog.size()).arg(request_.gaiaCsvPath));
    emit logLine(QStringLiteral("Calibrating against %1 sub(s)...\n").arg(subs.size()));

    const EquipmentCalibrationResult result =
        EquipmentCalibrator::calibrate(subs, catalog, request_.calibrationOptions);
    if (!result.ok) {
        emit logLine(QStringLiteral("error: %1\n").arg(result.errorMessage));
        emit finished(false);
        return;
    }

    QString reportText;
    QTextStream reportOut(&reportText);
    printCalibrateResult(result, subs.size(), reportOut);
    emit logLine(reportText);

    EquipmentProfile profile = result.profile;
    profile.label = request_.label;
    profile.telescopeApertureMm = request_.apertureMm;
    profile.focalLengthMm = request_.focalLengthMm;
    profile.correctorType = request_.correctorType.isEmpty() ? QStringLiteral("unknown") : request_.correctorType;
    profile.cameraModel = request_.cameraModel;
    profile.pixelSizeUm = request_.pixelSizeUm;
    profile.calibrationFilter = request_.filter;
    profile.validFrom = request_.validFrom;
    profile.validTo = request_.validTo;
    profile.referenceCatalogDescription =
        QStringLiteral("%1 (%2 stars)").arg(request_.gaiaCsvPath).arg(catalog.size());

    if (profile.correctorType == "refractive") {
        profile.chromaticCorrectorWarningShown = true;
        emit logLine(QStringLiteral(
            "\nNOTE: corrector type is 'refractive' -- a glass corrector/reducer/Barlow "
            "typically adds chromatic blur in broadband light. If this calibration session "
            "wasn't shot through a narrowband filter, a narrowband session (if you have one) "
            "will likely calibrate more precisely -- see docs/equipment-profiling-spec.md "
            "section 7.\n"));
    }

    if (!request_.outProfilePath.endsWith(".json", Qt::CaseInsensitive)) {
        emit logLine(QStringLiteral(
            "note: the output profile path doesn't end in .json -- it'll be saved as JSON "
            "regardless, but a .json extension is recommended so it's clear what it is later "
            "(this is the file the Date tab's \"Profile\" field wants).\n"));
    }

    QString saveErr;
    if (!EquipmentProfile::saveToFile(profile, request_.outProfilePath, &saveErr)) {
        emit logLine(QStringLiteral("\nerror: failed to save profile: %1\n").arg(saveErr));
        emit finished(false);
        return;
    }
    emit logLine(QStringLiteral("\nSaved equipment profile to %1\n").arg(request_.outProfilePath));

    if (!request_.residualsCsvPath.isEmpty()) {
        if (!request_.residualsCsvPath.endsWith(".csv", Qt::CaseInsensitive)) {
            emit logLine(QStringLiteral(
                "note: the residuals CSV path doesn't end in .csv -- it'll still be written as "
                "CSV, but a .csv extension is recommended so it's recognized by spreadsheet "
                "apps and by tools/residual-field.html's file picker.\n"));
        }
        QFile csvFile(request_.residualsCsvPath);
        if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit logLine(QStringLiteral("error: failed to open %1 for writing\n").arg(request_.residualsCsvPath));
            emit finished(false);
            return;
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
        emit logLine(QStringLiteral("Saved per-observation residuals (%1 rows) to %2\n")
                          .arg(result.observations.size())
                          .arg(request_.residualsCsvPath));
        emit logLine(QStringLiteral(
            "Open tools/residual-field.html in a browser and load this CSV to visualize it "
            "(radial vs. tangential pattern, per-axis/per-sub breakdown, before/after).\n"));
    }

    emit summaryReady(result.chosenOrder, result.rmsBeforeMas, result.profile.rmsAfterHeldoutMas,
                       result.profile.rmsAfterHeldoutStdMas, result.nSubsUsed, subs.size());
    emit finished(true);
}

} // namespace epochfrom::gui
