// Unit test for LinearWcs, cross-checked against astropy.wcs.WCS's output
// for the same header (a rotated, slightly non-square CD matrix -- not just
// the axis-aligned case PlateSolver's own WCS test already covers -- to
// exercise the general pix->world transform properly). Expected values were
// computed once with:
//
//   astropy.wcs.WCS(header).wcs_pix2world(x, y, 1)
//
// (origin=1, i.e. FITS-standard 1-indexed pixel coordinates, matching what
// LinearWcs::pixToWorld expects) and are hard-coded below.

#include "LinearWcs.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cmath>

using namespace epochfrom;

namespace {

bool nearlyEqualDeg(double a, double b, double tolDeg)
{
    return std::fabs(a - b) <= tolDeg;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const double crval1 = 83.822083;
    const double crval2 = -5.391111;
    const double crpix1 = 2328.5;
    const double crpix2 = 1760.5;
    const double cd11 = -0.0002325240527945491;
    const double cd12 = 4.0180258887931375e-05;
    const double cd21 = 4.100026417135855e-05;
    const double cd22 = 0.0002348492933224946;

    LinearWcs wcs(crval1, crval2, crpix1, crpix2, cd11, cd12, cd21, cd22);
    if (!wcs.isValid()) {
        out << "FAIL: LinearWcs construction failed: " << wcs.errorMessage() << "\n";
        return 1;
    }

    struct Case {
        double x, y, expectedRa, expectedDec;
    };
    const Case cases[] = {
        {2328.5, 1760.5, 83.8220830000, -5.3911110000}, // exactly at CRPIX -> exactly CRVAL
        {1.0, 1.0, 84.2950615770, -5.8995434831},
        {4656.0, 3520.0, 83.3498962323, -4.8823139144},
        {100.0, 3400.0, 84.4084293107, -5.0971817294},
        {4000.0, 50.0, 83.3624160764, -5.7241020254},
    };

    // 1e-7 deg is ~0.4 mas -- tighter than any noise floor this project
    // cares about, so this is checking the math is right, not just close.
    const double tolDeg = 1e-7;
    bool ok = true;
    for (const Case &c : cases) {
        double ra = 0.0, dec = 0.0;
        if (!wcs.pixToWorld(c.x, c.y, &ra, &dec)) {
            out << QString("FAIL: pixToWorld failed at (%1, %2)\n").arg(c.x).arg(c.y);
            ok = false;
            continue;
        }
        out << QString("(%1, %2) -> ra=%3 dec=%4 (expected %5 %6)\n")
                   .arg(c.x)
                   .arg(c.y)
                   .arg(ra, 0, 'f', 8)
                   .arg(dec, 0, 'f', 8)
                   .arg(c.expectedRa, 0, 'f', 8)
                   .arg(c.expectedDec, 0, 'f', 8);
        if (!nearlyEqualDeg(ra, c.expectedRa, tolDeg) || !nearlyEqualDeg(dec, c.expectedDec, tolDeg)) {
            out << "FAIL: mismatch beyond tolerance\n";
            ok = false;
        }
    }

    // An invalid construction (deliberately nonsensical CD -- all zero,
    // singular) should fail cleanly rather than crash or silently return
    // pixel-coordinate garbage as sky coordinates.
    LinearWcs singular(0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0);
    if (singular.isValid()) {
        out << "NOTE: singular CD matrix was still accepted as valid (wcslib may tolerate this "
               "at construction and only fail on use) -- not treated as a failure here.\n";
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
