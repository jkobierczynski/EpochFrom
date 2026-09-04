#pragma once

#include <QString>

namespace epochfrom {

// A pure linear (TAN, no SIP) tangent-plane WCS: CRVAL/CRPIX/CD only. This
// is deliberately NOT the same thing as PlateSolver::readWcsFile(), which
// honors a solved image's full TAN-SIP distortion terms -- equipment
// profiling needs exactly the opposite: the "before" baseline a solver's
// own linear WCS gives, with any distortion correction stripped away, so
// the residual against Gaia is the thing the SIP polynomial fit is meant to
// absorb. Matches the Python prototype's approach of building a fresh
// minimal header with only the linear keywords before feeding it to
// astropy's WCS.
//
// Implemented via wcslib (a tiny synthetic in-memory header is built from
// the 8 given numbers and parsed the same way PlateSolver does) rather than
// hand-derived gnomonic trig, so this shares the same well-tested
// projection math as the rest of the tool instead of a second
// hand-rolled implementation that could subtly disagree with it.
class LinearWcs {
public:
    LinearWcs(double crval1Deg, double crval2Deg, double crpix1, double crpix2, double cd11,
               double cd12, double cd21, double cd22);
    ~LinearWcs();

    LinearWcs(const LinearWcs &) = delete;
    LinearWcs &operator=(const LinearWcs &) = delete;

    bool isValid() const { return m_valid; }
    QString errorMessage() const { return m_errorMessage; }

    // pixX/pixY: FITS 1-indexed pixel coordinates (pixel (1,1) is the
    // center of the first pixel in the file, same convention CRPIX uses).
    // Returns false (and leaves outRaDeg/outDecDeg untouched) if the
    // underlying wcslib transform failed for this point.
    bool pixToWorld(double pixX, double pixY, double *outRaDeg, double *outDecDeg) const;

private:
    void *m_wcs = nullptr; // opaque struct wcsprm*, kept out of the header to avoid a wcslib include here
    bool m_valid = false;
    QString m_errorMessage;
};

} // namespace epochfrom
