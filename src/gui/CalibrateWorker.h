#pragma once

#include "EquipmentCalibrator.h"
#include "EquipmentProfile.h"
#include "GaiaCatalog.h"
#include "PlateSolver.h"

#include <QObject>
#include <QString>

namespace epochfrom::gui {

// Runs one `calibrate --dir` job on a worker thread: scan a directory,
// auto-solve any sub that doesn't have a .wcs sidecar yet, run
// EquipmentCalibrator, save the fitted profile, and optionally export a
// residuals CSV -- mirrors runCalibrate() in src/cli/main.cpp. Only the
// --dir input mode is exposed here (not the CLI's --sub/--wcs pair-list
// mode); that one's for hand-picking a scattered set of subs, which is
// enough of a power-user case that it can stay CLI-only for now.
class CalibrateWorker : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString dirPath;
        QString gaiaCsvPath;
        QString outProfilePath;
        QString residualsCsvPath; // empty = don't export

        epochfrom::PlateSolveOptions solveOptions; // for auto-solving subs missing a .wcs
        epochfrom::EquipmentCalibrationOptions calibrationOptions;

        // Profile metadata -- purely descriptive, the calibrator doesn't
        // touch these (see EquipmentProfile.h).
        QString label;
        double apertureMm = 0.0;
        double focalLengthMm = 0.0;
        QString correctorType; // "" (-> "unknown"), "none", "refractive", "reflective", "catadioptric"
        QString cameraModel;
        double pixelSizeUm = 0.0;
        QString filter;
        QString validFrom;
        QString validTo;
    };

    explicit CalibrateWorker(Request request, QObject *parent = nullptr)
        : QObject(parent), request_(std::move(request)) {}

public slots:
    void run();

signals:
    void logLine(const QString &text);
    // Primitive fields only (not the full result struct) so this can cross
    // the thread boundary with a plain queued signal/slot connection,
    // without registering a custom Qt metatype for it.
    void summaryReady(int chosenOrder, double rmsBeforeMas, double rmsAfterHeldoutMas,
                       double rmsAfterHeldoutStdMas, int nSubsUsed, int nSubsTotal);
    void finished(bool ok);

private:
    Request request_;
};

} // namespace epochfrom::gui
