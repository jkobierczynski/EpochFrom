#pragma once

#include "EquipmentProfile.h"
#include "GaiaCatalog.h"
#include "ImageDater.h"
#include "PlateSolver.h"

#include <QObject>
#include <QString>

namespace epochfrom::gui {

// Runs one `date` job (single image or --dir batch) on a worker thread --
// mirrors runDate()/runDateOne()/runDateDir() in src/cli/main.cpp. Loading
// the Gaia catalog and (if given) the equipment profile happens here too,
// since both can take a moment for a large catalog and this all needs to
// stay off the GUI thread regardless.
class DateWorker : public QObject {
    Q_OBJECT
public:
    struct Request {
        bool isDir = false;
        QString path; // single image, or a directory
        QString wcsPath; // single-image mode only; empty = derive <image>.wcs next to it
        QString gaiaCsvPath;
        QString profilePath; // empty = no profile (equivalent of CLI's --noprofile)

        epochfrom::PlateSolveOptions solveOptions; // for auto-solving a missing .wcs
        epochfrom::DateEstimateOptions dateOptions; // .equipmentProfile is set by run(), ignore it here
    };

    explicit DateWorker(Request request, QObject *parent = nullptr)
        : QObject(parent), request_(std::move(request)) {}

public slots:
    void run();

signals:
    void logLine(const QString &text);
    // Single-image mode only: the headline numbers, as primitives, for the
    // tab's at-a-glance summary fields.
    void summaryReady(const QString &estimatedDate, double epochJyear, double epochSigmaYears,
                       double rmsResidualMas);
    void finished(bool ok);

private:
    Request request_;
};

} // namespace epochfrom::gui
