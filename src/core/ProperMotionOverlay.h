#pragma once

#include "GaiaCatalog.h"

#include <QString>
#include <QVector>

namespace epochfrom {

class Wcs;

// One "fastest mover" to draw on a starfield view: a Gaia star's pixel
// position at the display epoch (where the yellow circle goes), the unit
// direction it's moving in on this frame's pixel grid ("going to" -- where
// the green line points), and how long the line should be drawn. The red
// "coming from" line is just this direction negated, drawn from the same
// center -- callers don't need a second vector for it.
struct ProperMotionArrow {
    qint64 sourceId = 0;
    double centerPixX = 0.0; // FITS 1-indexed pixel coords, same convention as Wcs::pixToWorld
    double centerPixY = 0.0;
    double dirPixX = 0.0; // unit vector, "going to" direction in pixel space
    double dirPixY = 0.0;
    double lengthPix = 0.0; // center-to-tip length, already scaled (see ProperMotionOverlayOptions)
    double pmTotalMasYr = 0.0;
    double photGMeanMag = 99.0;
};

struct ProperMotionOverlayOptions {
    // How many of the fastest-moving stars actually visible in this frame
    // to return.
    int topN = 15;

    // Epoch to place the yellow circle at (Julian year) -- normally the
    // capture's own DATE-OBS, but any epoch can be asked for (e.g. to
    // preview where these stars will be decades from now).
    double targetEpochJyear = 2000.0;

    // Image bounds in FITS pixel coordinates, used to keep only stars that
    // actually land inside this frame -- a Gaia query is normally a wide
    // cone around the field center (see scripts/gaia_field_query.py
    // --radius), well beyond the sensor's actual footprint, so ranking by
    // proper motion has to happen after that filter or "top N" would mean
    // "top N in the query cone," not "top N in this image." 0x0 disables
    // the bounds check (every projectable star is a candidate).
    int imageWidth = 0;
    int imageHeight = 0;

    // Average line half-length across the selected stars, in image pixels.
    // Each star's own line is this scaled by its pm_total relative to the
    // group's average, so the fastest mover in the set gets the longest
    // line and the slowest gets the shortest, while the *set's average*
    // length is pinned to this value. The caller picks it -- e.g. the
    // image diagonal / 30, which is what StarfieldWorker uses by default.
    double averageArrowLengthPix = 0.0;

    // How far forward/back (Julian years) to propagate a star when
    // measuring the *direction* of its motion in pixel space. Needs to be
    // large enough that the resulting pixel displacement is well above
    // WCS/roundoff noise for every star in the catalog (including slow
    // ones near the topN cutoff), and small enough that linear space
    // motion is still an excellent approximation over the span -- 200
    // years comfortably satisfies both for any real Gaia DR3 field.
    double directionBaselineYears = 200.0;
};

class ProperMotionOverlay {
public:
    // Ranks `stars` by total proper motion (hypot(pmra, pmdec)) among those
    // that land inside the image bounds at targetEpochJyear, keeps the
    // fastest `topN`, and builds a draw-ready arrow for each. Returns an
    // empty list (with *warning set) if no candidate star lands in frame.
    // *warning is also set (non-fatal) when fewer than topN candidates were
    // available. Returns an empty list without setting *warning only if
    // `wcs` itself isn't valid, in which case *warning carries that error
    // instead.
    static QVector<ProperMotionArrow> compute(const QVector<GaiaStar> &stars, const Wcs &wcs,
                                               const ProperMotionOverlayOptions &options,
                                               QString *warning = nullptr);
};

} // namespace epochfrom
