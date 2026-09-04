#pragma once

#include <QString>
#include <limits>

namespace epochfrom {

struct PlateSolveResult {
    bool solved = false;
    double centerRaDeg = 0.0;
    double centerDecDeg = 0.0;
    double fieldWidthArcmin = 0.0;
    double fieldHeightArcmin = 0.0;
    double pixelScaleArcsecPerPix = 0.0;
    int imageWidthPx = 0;
    int imageHeightPx = 0;
    QString wcsFilePath;  // the .wcs sidecar this was read from
    QString errorMessage; // set when solved == false

    // Raw linear (TAN, no SIP) WCS terms straight from the header, exposed
    // for callers that need the solver's *linear* WCS specifically rather
    // than the SIP-corrected center above -- equipment profiling is exactly
    // this case: it needs the "before" baseline a plain linear WCS gives
    // (see LinearWcs) so the residual against Gaia is the thing the SIP
    // polynomial fit is meant to absorb. centerRaDeg/centerDecDeg above
    // still honor SIP when present, since that's the more accurate answer
    // for ordinary dating use.
    double crval1Deg = 0.0;
    double crval2Deg = 0.0;
    double crpix1 = 0.0;
    double crpix2 = 0.0;
    double cd11 = 0.0;
    double cd12 = 0.0;
    double cd21 = 0.0;
    double cd22 = 0.0;
};

struct PlateSolveOptions {
    // Optional pointing hint to narrow (and greatly speed up) the search --
    // same idea as the Python prototype's --ra/--dec/--radius. Leave
    // hintRaDeg as NaN (the default) for a blind solve.
    double hintRaDeg = std::numeric_limits<double>::quiet_NaN();
    double hintDecDeg = std::numeric_limits<double>::quiet_NaN();
    double hintRadiusDeg = 1.0;

    // Optional pixel-scale bounds, arcsec/pixel. Leave both NaN to let
    // solve-field search its full default range (correct but much slower).
    double scaleLowArcsecPerPix = std::numeric_limits<double>::quiet_NaN();
    double scaleHighArcsecPerPix = std::numeric_limits<double>::quiet_NaN();

    int downsample = 2;
    int cpuLimitSeconds = 55;
    QString solveFieldPath = QStringLiteral("solve-field"); // resolved via PATH by default
};

class PlateSolver {
public:
    // Shells out to astrometry.net's solve-field on `imagePath` and blocks
    // until it finishes (or times out). Deliberately not linked against a
    // solver library directly -- see the project README for why. Requires
    // solve-field (and appropriate index files for the field's scale) to
    // be installed and reachable via PATH, or options.solveFieldPath.
    static PlateSolveResult solve(const QString &imagePath, const PlateSolveOptions &options = {});

    // Reads center RA/Dec, field size, and pixel scale directly out of an
    // already-solved .wcs sidecar file (astrometry.net's own output
    // format: a headers-only FITS file carrying CRVAL/CRPIX/CD[/SIP] plus
    // its own IMAGEW/IMAGEH keywords). Uses wcslib for the pixel->world
    // conversion, so this respects the full TAN or TAN-SIP projection
    // rather than a naive linear shortcut. Public (not just an internal
    // step of solve()) since re-reading an existing .wcs file without
    // re-running the solver is a legitimate thing to want to do on its
    // own -- and it's what lets this be tested without depending on
    // solve-field being installed at all.
    static PlateSolveResult readWcsFile(const QString &wcsPath);
};

} // namespace epochfrom
