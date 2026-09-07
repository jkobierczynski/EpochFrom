#include "StarfieldWorker.h"

#include "FitsImage.h"
#include "GaiaCatalog.h"
#include "ImageStretch.h"
#include "ProperMotionOverlay.h"
#include "Wcs.h"

#include <QDir>
#include <QFileInfo>
#include <QMetaType>

#include <cmath>

namespace epochfrom::gui {

StarfieldWorker::StarfieldWorker(Request request, QObject *parent)
    : QObject(parent), request_(std::move(request))
{
    // Registered here (constructor still runs on the GUI thread, before
    // moveToThread()/start()) so the Result struct is a known QMetaType
    // before run() ever emits it across the thread boundary.
    qRegisterMetaType<Result>("epochfrom::gui::StarfieldWorker::Result");
}

void StarfieldWorker::run()
{
    Result result;

    const QFileInfo imageInfo(request_.imagePath);
    if (!imageInfo.exists()) {
        result.errorMessage = QStringLiteral("image not found: %1").arg(request_.imagePath);
        emit finished(result);
        return;
    }

    const QString wcsPath = !request_.wcsPath.isEmpty()
                                 ? request_.wcsPath
                                 : imageInfo.dir().filePath(imageInfo.completeBaseName() + ".wcs");
    if (!QFileInfo::exists(wcsPath)) {
        result.errorMessage =
            QStringLiteral("no .wcs file found at %1 -- solve this image first (Solve tab)").arg(wcsPath);
        emit finished(result);
        return;
    }

    const epochfrom::FitsImageData image = epochfrom::FitsImage::load(request_.imagePath);
    if (!image.loaded) {
        result.errorMessage = image.errorMessage.isEmpty()
                                   ? QStringLiteral("failed to load FITS image")
                                   : QStringLiteral("failed to load FITS image: %1").arg(image.errorMessage);
        emit finished(result);
        return;
    }

    double targetEpoch = request_.epochOverrideJyear;
    if (std::isnan(targetEpoch)) {
        if (std::isnan(image.dateObsJyear)) {
            result.errorMessage =
                QStringLiteral("this image has no readable DATE-OBS -- set an epoch override above");
            emit finished(result);
            return;
        }
        targetEpoch = image.dateObsJyear;
    }

    QVector<epochfrom::GaiaStar> stars;
    QString gaiaError;
    if (!epochfrom::GaiaCatalog::loadCsv(request_.gaiaCsvPath, &stars, &gaiaError)) {
        result.errorMessage = QStringLiteral("failed to load Gaia catalog: %1").arg(gaiaError);
        emit finished(result);
        return;
    }
    result.nCatalogStars = stars.size();

    const epochfrom::Wcs wcs(wcsPath);
    if (!wcs.isValid()) {
        result.errorMessage = QStringLiteral("failed to read WCS %1: %2").arg(wcsPath, wcs.errorMessage());
        emit finished(result);
        return;
    }

    result.stretchedPixels = epochfrom::ImageStretch::autoStretch(image);
    result.imageWidth = image.width;
    result.imageHeight = image.height;

    // "Average 1/30 of the size of the capture," per the feature request --
    // the diagonal is used as that overall size measure so the scaling
    // stays sensible regardless of the sensor's aspect ratio.
    const double diagonal = std::hypot(static_cast<double>(image.width), static_cast<double>(image.height));
    result.averageArrowLengthPix = diagonal / 30.0;
    result.circleRadiusPix = std::max(result.averageArrowLengthPix / 6.0, 4.0);

    epochfrom::ProperMotionOverlayOptions options;
    options.topN = request_.topN;
    options.targetEpochJyear = targetEpoch;
    options.imageWidth = image.width;
    options.imageHeight = image.height;
    options.averageArrowLengthPix = result.averageArrowLengthPix;

    QString overlayWarning;
    result.arrows = epochfrom::ProperMotionOverlay::compute(stars, wcs, options, &overlayWarning);
    result.warning = overlayWarning;
    result.usedEpochJyear = targetEpoch;

    if (result.arrows.isEmpty() && !overlayWarning.isEmpty()) {
        result.errorMessage = overlayWarning;
        emit finished(result);
        return;
    }

    result.ok = true;
    emit finished(result);
}

} // namespace epochfrom::gui
