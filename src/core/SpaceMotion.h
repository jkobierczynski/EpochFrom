#pragma once

#include "GaiaCatalog.h"

namespace epochfrom {

// Propagates a Gaia star's position from its catalog reference epoch to any
// other epoch, accounting for proper motion (and, where available,
// parallax + radial velocity via the "perspective acceleration" effect of a
// star's changing distance and foreshortened angle over time).
//
// This is the "rigorous" method used in Hipparcos/Gaia epoch-propagation
// documentation (and what astropy's SkyCoord.apply_space_motion does under
// the hood): convert to a 3D Cartesian position + velocity vector, advance
// linearly in Cartesian space, convert back to spherical coordinates. This
// is NOT the same as naively adding pmra*dt to ra and pmdec*dt to dec on
// the sky, which is only a flat-tangent-plane approximation -- it matters
// at the sub-mas-per-year level for nearby/fast stars, and this project's
// Python prototype validated against exactly this method, so the C++ port
// needs to match it to reproduce the same numbers.
class SpaceMotion {
public:
    struct Position {
        double raDeg = 0.0;
        double decDeg = 0.0;
    };

    // epochJyear: target epoch, Julian year (e.g. 2018.6677 for a frame's
    // DATE-OBS converted via astropy-equivalent Time(...).jyear).
    //
    // Stars with no measured (or non-positive) parallax are given a large
    // fallback distance (3000 pc), matching the Python prototype's
    // build_gaia_skycoord(): at these baselines (years, not millennia) the
    // propagated *direction* barely depends on distance for a star this
    // far away -- only the (already tiny) perspective-acceleration term
    // does, and it's negligible for an unmeasured/very-low-parallax star
    // regardless of what fallback distance is used.
    static Position propagate(const GaiaStar &star, double epochJyear);
};

} // namespace epochfrom
