#pragma once

#include <QString>

namespace epochfrom {

// General pixel -> world (RA/Dec) converter for an existing .wcs sidecar
// file, honoring the full TAN or TAN-SIP projection -- SIP distortion
// terms included when the solver wrote them -- via wcslib. This is the
// same wcslib-based conversion PlateSolver::readWcsFile() already uses
// internally to compute a field's center RA/Dec, generalized here to
// arbitrary pixel coordinates and kept alive across many calls (rather
// than parsed once for a single point and discarded).
//
// Use this -- as opposed to LinearWcs, which is deliberately linear-only --
// when dating an image WITHOUT a separately Gaia-calibrated
// EquipmentProfile: it relies on whatever the platesolver itself fit (a
// low-order SIP tweak against its own sparse index catalog, if any),
// which is the fallback baseline docs/equipment-profiling-spec.md section
// 9 describes an EquipmentProfile's polynomial correction as improving on.
class Wcs {
public:
    explicit Wcs(const QString &wcsPath);
    ~Wcs();
    Wcs(const Wcs &) = delete;
    Wcs &operator=(const Wcs &) = delete;

    bool isValid() const { return m_valid; }
    QString errorMessage() const { return m_errorMessage; }

    // pixX/pixY are FITS 1-indexed pixel coordinates.
    bool pixToWorld(double pixX, double pixY, double *outRaDeg, double *outDecDeg) const;

private:
    void *m_wcs = nullptr; // opaque Holder{wcsprm*, nwcs}, see LinearWcs.cpp for the same pattern
    bool m_valid = false;
    QString m_errorMessage;
};

} // namespace epochfrom
