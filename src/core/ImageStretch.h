#pragma once

#include "FitsImage.h"

#include <QVector>

namespace epochfrom {

// Turns a loaded FITS image's raw float pixel data into an 8-bit
// display-ready grayscale buffer -- same width/height and the same
// row-major order as FitsImageData::pixels (row 0 = FITS pixel y=1, i.e.
// NOT yet flipped for on-screen display; the GUI layer flips it when
// building a QImage, see StarfieldCanvas.cpp, so this stays pure Qt::Core
// like the rest of this library).
//
// Uses a simple, robust auto-stretch: clip to a low and high percentile of
// the pixel histogram (defaults 0.25% / 99.75% -- wide enough to survive a
// handful of hot pixels or saturated stars without letting them wash out
// the whole stretch), then apply an asinh (inverse hyperbolic sine)
// transfer function. That's a standard nonlinear astro-display stretch: it
// compresses a bright star's core while still lifting faint nebulosity and
// background stars out of the noise floor, unlike a plain linear min/max
// stretch which either buries the background or blows out every star.
// Deliberately a free-standing (not nested) struct: a nested type's default
// member initializers aren't usable in a default *argument* of the
// enclosing class (needed below for autoStretch's `= Options()`) until
// after the enclosing class is fully defined, which is a chicken-and-egg
// problem right at the point of declaring that default argument.
struct ImageStretchOptions {
    double lowPercentile = 0.25;
    double highPercentile = 99.75;
    // Larger = more aggressive nonlinear lift of faint signal (and more
    // compression of bright cores). 10 is a reasonable general-purpose
    // default for a stretched preview, similar in effect to the
    // "asinh(x*A)/asinh(A)" curve used by ds9/PixInsight-style STF
    // previews.
    double asinhAmount = 10.0;
};

class ImageStretch {
public:
    using Options = ImageStretchOptions;

    // Returns a width*height buffer of bytes, 0-255. Empty if `image` isn't
    // loaded or has no pixels.
    static QVector<unsigned char> autoStretch(const FitsImageData &image,
                                               const Options &options = Options());
};

} // namespace epochfrom
