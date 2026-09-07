#include "StarfieldCanvas.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace epochfrom::gui {

namespace {
constexpr double kMinScale = 0.02;
constexpr double kMaxScale = 64.0;
} // namespace

StarfieldCanvas::StarfieldCanvas(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(200);
    setFocusPolicy(Qt::StrongFocus);
}

void StarfieldCanvas::setImage(const QVector<unsigned char> &stretchedPixels, int width, int height)
{
    imageWidth_ = width;
    imageHeight_ = height;
    arrows_.clear();

    if (width <= 0 || height <= 0 || stretchedPixels.size() < static_cast<qsizetype>(width) * height) {
        image_ = QImage();
        update();
        return;
    }

    // FITS row 0 is pixel y=1 (bottom of the sky-projected frame); on-screen
    // row 0 is the top. Flip while copying so image_ is display-oriented
    // once and for all -- everything downstream (paintEvent, the
    // widget<->image transforms) can then treat it as an ordinary top-down
    // raster.
    QImage img(width, height, QImage::Format_Grayscale8);
    for (int fitsRow = 0; fitsRow < height; ++fitsRow) {
        const int dispRow = height - 1 - fitsRow;
        std::memcpy(img.scanLine(dispRow), stretchedPixels.constData() + static_cast<qsizetype>(fitsRow) * width,
                    static_cast<size_t>(width));
    }
    image_ = img;

    hasFitOnce_ = false;
    zoomToFit();
    update();
}

void StarfieldCanvas::setArrows(const QVector<epochfrom::ProperMotionArrow> &arrows, double circleRadiusPix)
{
    arrows_ = arrows;
    circleRadiusPix_ = std::max(circleRadiusPix, 1.0);
    update();
}

void StarfieldCanvas::clear()
{
    image_ = QImage();
    imageWidth_ = 0;
    imageHeight_ = 0;
    arrows_.clear();
    hasFitOnce_ = false;
    update();
}

void StarfieldCanvas::zoomToFit()
{
    if (imageWidth_ <= 0 || imageHeight_ <= 0 || width() <= 0 || height() <= 0)
        return;

    const double sx = static_cast<double>(width()) / imageWidth_;
    const double sy = static_cast<double>(height()) / imageHeight_;
    scale_ = std::clamp(std::min(sx, sy) * 0.98, kMinScale, kMaxScale);

    const double viewImgW = width() / scale_;
    const double viewImgH = height() / scale_;
    topLeftImagePt_ = QPointF((imageWidth_ - viewImgW) / 2.0, (imageHeight_ - viewImgH) / 2.0);
    hasFitOnce_ = true;
    update();
}

QPointF StarfieldCanvas::widgetToImage(const QPointF &widgetPt) const
{
    return QPointF(widgetPt.x() / scale_ + topLeftImagePt_.x(), widgetPt.y() / scale_ + topLeftImagePt_.y());
}

QPointF StarfieldCanvas::imageToWidget(const QPointF &imagePt) const
{
    return QPointF((imagePt.x() - topLeftImagePt_.x()) * scale_, (imagePt.y() - topLeftImagePt_.y()) * scale_);
}

void StarfieldCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(18, 18, 22));

    if (image_.isNull()) {
        painter.setPen(QColor(150, 150, 160));
        painter.drawText(rect(), Qt::AlignCenter,
                          tr("Load a solved image + Gaia catalog above to see the starfield"));
        return;
    }

    QTransform t;
    t.scale(scale_, scale_);
    t.translate(-topLeftImagePt_.x(), -topLeftImagePt_.y());
    painter.setWorldTransform(t);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(QRectF(0, 0, imageWidth_, imageHeight_), image_);

    QPen circlePen(QColor(255, 221, 0));
    circlePen.setWidthF(2.0);
    circlePen.setCosmetic(true);
    QPen goingPen(QColor(70, 220, 100));
    goingPen.setWidthF(2.2);
    goingPen.setCosmetic(true);
    QPen fromPen(QColor(235, 70, 70));
    fromPen.setWidthF(2.2);
    fromPen.setCosmetic(true);

    const QColor goingColor(70, 220, 100);

    for (const epochfrom::ProperMotionArrow &arrow : arrows_) {
        // FITS pixel space (y up) -> display/image space (y down): x is
        // unchanged, y flips sign for a displacement, or maps as
        // (height - fitsY) for an absolute point -- same convention
        // setImage() used to flip the raster itself.
        const QPointF center(arrow.centerPixX - 1.0, imageHeight_ - arrow.centerPixY);
        const QPointF dir(arrow.dirPixX, -arrow.dirPixY);

        // Lines start at the circle's own boundary, not its center, so they
        // visibly touch the yellow circle rather than run underneath it;
        // each line's own drawn length still equals arrow.lengthPix.
        const QPointF goingStart = center + dir * circleRadiusPix_;
        const QPointF comingStart = center - dir * circleRadiusPix_;
        const QPointF tip = goingStart + dir * arrow.lengthPix;
        const QPointF tail = comingStart - dir * arrow.lengthPix;

        painter.setPen(goingPen);
        painter.drawLine(goingStart, tip);
        painter.setPen(fromPen);
        painter.drawLine(comingStart, tail);

        // Small filled arrowhead at the tip of the green line, showing
        // which way the star is heading. Sized off the circle radius (so
        // it stays a consistent, visible size across arrows regardless of
        // each one's own length) but capped to a fraction of the line's
        // own length so it doesn't swallow a very short line whole.
        const double headLen = std::min(circleRadiusPix_ * 1.1, arrow.lengthPix * 0.6);
        if (headLen > 0.75) {
            const QPointF perp(-dir.y(), dir.x());
            const double headHalfWidth = headLen * 0.55;
            const QPointF headBase = tip - dir * headLen;
            QPainterPath headPath;
            headPath.moveTo(tip);
            headPath.lineTo(headBase + perp * headHalfWidth);
            headPath.lineTo(headBase - perp * headHalfWidth);
            headPath.closeSubpath();
            painter.setPen(Qt::NoPen);
            painter.setBrush(goingColor);
            painter.drawPath(headPath);
        }

        painter.setPen(circlePen);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, circleRadiusPix_, circleRadiusPix_);
    }

    // Legend, drawn in plain widget space (not the zoomed/panned transform)
    // so it stays fixed-size and readable at any zoom.
    painter.resetTransform();
    const int margin = 10;
    const int lineH = 18;
    QFont legendFont = painter.font();
    legendFont.setPointSizeF(legendFont.pointSizeF() * 0.92);
    painter.setFont(legendFont);
    struct LegendRow {
        QColor color;
        QString label;
    };
    const QVector<LegendRow> rows = {
        {QColor(255, 221, 0), tr("star (top proper motion)")},
        {QColor(70, 220, 100), tr("direction of motion")},
        {QColor(235, 70, 70), tr("where it came from")},
    };
    const int boxW = 230;
    const int boxH = margin * 2 + lineH * rows.size();
    QRect legendRect(margin, height() - boxH - margin, boxW, boxH);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawRoundedRect(legendRect, 6, 6);
    for (int i = 0; i < rows.size(); ++i) {
        const int y = legendRect.top() + margin + i * lineH;
        QPen pen(rows[i].color);
        pen.setWidthF(2.5);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(legendRect.left() + margin, y + lineH / 2, legendRect.left() + margin + 20,
                          y + lineH / 2);
        painter.setPen(QColor(230, 230, 235));
        painter.drawText(legendRect.left() + margin + 28, y, boxW - margin * 2 - 28, lineH,
                          Qt::AlignVCenter | Qt::AlignLeft, rows[i].label);
    }
}

void StarfieldCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!hasFitOnce_)
        zoomToFit();
}

void StarfieldCanvas::wheelEvent(QWheelEvent *event)
{
    if (image_.isNull()) {
        event->ignore();
        return;
    }

    const QPointF cursorWidgetPt = event->position();
    const QPointF cursorImagePt = widgetToImage(cursorWidgetPt);

    const double steps = event->angleDelta().y() / 120.0;
    const double factor = std::pow(1.2, steps);
    scale_ = std::clamp(scale_ * factor, kMinScale, kMaxScale);

    topLeftImagePt_ = QPointF(cursorImagePt.x() - cursorWidgetPt.x() / scale_,
                               cursorImagePt.y() - cursorWidgetPt.y() / scale_);
    update();
    event->accept();
}

void StarfieldCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !image_.isNull()) {
        panning_ = true;
        lastPanPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void StarfieldCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (panning_) {
        const QPoint delta = event->pos() - lastPanPos_;
        topLeftImagePt_ -= QPointF(delta.x() / scale_, delta.y() / scale_);
        lastPanPos_ = event->pos();
        update();
        event->accept();
        return;
    }

    const int idx = arrowUnderCursor(event->position());
    if (idx >= 0) {
        const epochfrom::ProperMotionArrow &a = arrows_[idx];
        QToolTip::showText(event->globalPosition().toPoint(),
                            tr("Gaia %1\nG = %2\npm = %3 mas/yr")
                                .arg(a.sourceId)
                                .arg(QString::number(a.photGMeanMag, 'f', 2))
                                .arg(QString::number(a.pmTotalMasYr, 'f', 1)),
                            this);
    } else {
        QToolTip::hideText();
    }
    QWidget::mouseMoveEvent(event);
}

void StarfieldCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && panning_) {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void StarfieldCanvas::leaveEvent(QEvent *event)
{
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

int StarfieldCanvas::arrowUnderCursor(const QPointF &widgetPt) const
{
    constexpr double kHitRadiusWidgetPx = 14.0;
    int best = -1;
    double bestDist = kHitRadiusWidgetPx;
    for (int i = 0; i < arrows_.size(); ++i) {
        const QPointF center(arrows_[i].centerPixX - 1.0, imageHeight_ - arrows_[i].centerPixY);
        const QPointF w = imageToWidget(center);
        const double d = std::hypot(w.x() - widgetPt.x(), w.y() - widgetPt.y());
        if (d < bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

} // namespace epochfrom::gui
