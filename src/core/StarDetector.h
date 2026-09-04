#pragma once

#include "FitsImage.h"

#include <QVector>

namespace epochfrom {

struct DetectedStar {
    // 0-indexed pixel coordinates, same convention as FitsImageData::pixels
    // (col 0 = FITS x=1, row 0 = FITS y=1) -- add 1 to get a FITS/WCS pixel
    // coordinate, exactly like photutils' xcentroid/ycentroid did in the
    // Python prototype.
    double x = 0.0;
    double y = 0.0;
    double flux = 0.0;        // background-subtracted, summed over the centroiding window
    double fwhmEstimatePx = 0.0; // from the window's 2nd moments -- a rough size check, not a PSF fit
};

struct StarDetectionOptions {
    double fwhmPx = 4.0;       // expected stellar FWHM, pixels -- sets the matched-filter kernel size
    double thresholdSigma = 6.0; // detection threshold, multiples of background noise
    // DAOFIND-style sharpness cut (Stetson 1987): reject a candidate peak
    // if (raw center - mean of its 8 raw neighbors) / (matched-filtered
    // peak height) falls outside [sharpLow, sharpHigh]. This is what
    // actually discriminates true point sources from extended nebula
    // texture and single-pixel cosmic rays/hot pixels -- see the
    // implementation for the reasoning. Defaults match photutils'
    // DAOStarFinder defaults.
    double sharpLow = 0.2;
    double sharpHigh = 1.0;
    // Reject candidates whose window-moment size falls outside
    // [fwhmPx*sizeRatioLow, fwhmPx*sizeRatioHigh] -- a secondary guard
    // against blends/saturation/chip defects, on top of the sharpness cut
    // above.
    double sizeRatioLow = 0.35;
    double sizeRatioHigh = 2.5;
    int centroidIterations = 2;
};

// Point-source detector: a coarse tiled + bilinearly-interpolated local
// background estimate (needed because this project's actual subject matter
// -- emission nebulae -- have broad, smoothly-varying glow a single
// frame-wide background level doesn't remove), a Gaussian matched-filter
// convolution sized to the expected FWHM for point-source SNR, local-maximum
// peak finding gated by a DAOFIND-style sharpness statistic (the real
// discriminator against nebula texture and cosmic rays), then iterative
// flux-weighted sub-pixel centroiding on the original (unsmoothed)
// background-subtracted data within a window around each peak. Same overall
// family of technique as DAOStarFinder/DAOPHOT, not a bit-exact port of it.
class StarDetector {
public:
    static QVector<DetectedStar> detect(const FitsImageData &image,
                                          const StarDetectionOptions &options = {});
};

} // namespace epochfrom
