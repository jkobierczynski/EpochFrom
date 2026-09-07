#pragma once

#include "ProperMotionOverlay.h"

#include <QImage>
#include <QPointF>
#include <QVector>
#include <QWidget>

namespace epochfrom::gui {

// Pan/zoom starfield viewer: draws a stretched FITS background image with
// each ProperMotionOverlay arrow superimposed -- a yellow circle around the
// star's position at the display epoch, a green line pointing where it's
// heading, and a red line pointing where it came from (same axis as the
// green line, just the other way, per ProperMotionOverlay's contract).
//
// All the FITS-pixel-to-display-space bookkeeping (FITS pixels are
// 1-indexed with y increasing upward; on-screen/QImage rows increase
// downward from 0) lives in this one class, so StarfieldWorker/StarfieldTab
// never have to think about it -- they hand over the raw stretched bytes
// (same row order as FitsImageData::pixels) and arrows straight from
// ProperMotionOverlay, in FITS pixel coordinates.
class StarfieldCanvas : public QWidget {
    Q_OBJECT
public:
    explicit StarfieldCanvas(QWidget *parent = nullptr);

    // `stretchedPixels` is a width*height 8-bit grayscale buffer, row 0 =
    // FITS pixel y=1 (same convention as FitsImageData::pixels / what
    // ImageStretch::autoStretch returns). Resets the view to fit the whole
    // image.
    void setImage(const QVector<unsigned char> &stretchedPixels, int width, int height);

    // Arrows are in FITS pixel coordinates (ProperMotionOverlay's native
    // output, no conversion needed by the caller). `circleRadiusPix` is
    // also in FITS/image pixel units, so it scales naturally with zoom.
    void setArrows(const QVector<epochfrom::ProperMotionArrow> &arrows, double circleRadiusPix);

    void clear();
    void zoomToFit();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QPointF widgetToImage(const QPointF &widgetPt) const;
    QPointF imageToWidget(const QPointF &imagePt) const;
    int arrowUnderCursor(const QPointF &widgetPt) const;

    QImage image_; // already display-oriented (row 0 = top of frame)
    int imageWidth_ = 0;
    int imageHeight_ = 0;
    QVector<epochfrom::ProperMotionArrow> arrows_;
    double circleRadiusPix_ = 10.0;

    double scale_ = 1.0;           // widget pixels per image pixel
    QPointF topLeftImagePt_{0, 0}; // image-space point shown at widget (0,0)

    bool panning_ = false;
    QPoint lastPanPos_;
    bool hasFitOnce_ = false;
};

} // namespace epochfrom::gui
