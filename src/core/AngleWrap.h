#pragma once

namespace epochfrom {

// Wraps a right-ascension difference (in degrees, "this minus that") into
// (-180, 180]. RA is a periodic coordinate (0 deg and 360 deg are the same
// point on the sky), but a plain subtraction of two RA values doesn't know
// that: for two genuinely nearby stars that happen to sit on opposite
// sides of the 0/360 branch cut (e.g. one WCS solution reporting 359.9998
// and another, for the same star, reporting 0.0006), a raw difference of
// ~-360 degrees survives all the way through "* cos(dec) * 3.6e6" into a
// residual of hundreds of millions of milliarcseconds -- degrees, not
// arcseconds, off -- even though the two positions are actually a couple
// of milliarcseconds apart. This bit a real user calibrating a target
// right at that boundary (Cederblad 214 / NGC 7822, RA ~0.9 deg): one
// star's per-sub "before" residual came out ~500 million mas instead of
// the couple-thousand its neighbors showed, from exactly this. Any code
// that turns two independently-computed RA values into a tangent-plane
// offset must wrap the difference through this first.
inline double wrapRaDiffDeg(double diffDeg)
{
    while (diffDeg > 180.0)
        diffDeg -= 360.0;
    while (diffDeg <= -180.0)
        diffDeg += 360.0;
    return diffDeg;
}

} // namespace epochfrom
