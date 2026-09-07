#pragma once

#include "ProperMotionOverlay.h"

#include <QObject>
#include <QString>
#include <QVector>
#include <limits>

namespace epochfrom::gui {

// Loads everything a StarfieldTab needs off the UI thread: the FITS image
// (for the stretched background raster), its .wcs sidecar, and a Gaia
// catalog CSV, then computes the top-N proper-motion arrows to overlay.
// Meant to be moved to a QThread and run() once, same pattern as
// SolveWorker/DateWorker/CalibrateWorker.
class StarfieldWorker : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString imagePath;
        QString wcsPath; // empty = default to "<image>.wcs" next to imagePath
        QString gaiaCsvPath;
        int topN = 15;
        // NaN = use the image's own DATE-OBS.
        double epochOverrideJyear = std::numeric_limits<double>::quiet_NaN();
    };

    struct Result {
        bool ok = false;
        QString errorMessage;
        QString warning; // non-fatal, e.g. "only 8 stars available"

        int imageWidth = 0;
        int imageHeight = 0;
        QVector<unsigned char> stretchedPixels; // FITS row order, see ImageStretch.h

        QVector<epochfrom::ProperMotionArrow> arrows;
        double usedEpochJyear = 0.0;
        double averageArrowLengthPix = 0.0;
        double circleRadiusPix = 0.0;
        int nCatalogStars = 0;
    };

    explicit StarfieldWorker(Request request, QObject *parent = nullptr);

public slots:
    void run();

signals:
    void finished(epochfrom::gui::StarfieldWorker::Result result);

private:
    Request request_;
};

} // namespace epochfrom::gui

Q_DECLARE_METATYPE(epochfrom::gui::StarfieldWorker::Result)
