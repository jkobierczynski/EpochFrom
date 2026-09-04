#include "SpaceMotion.h"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>

namespace epochfrom {

namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

// mas/yr -> rad/yr
constexpr double kMasYrToRadYr = (M_PI / 180.0) / 3600.0 / 1000.0;

// 1 AU/yr in km/s (IAU-standard conversion constant used throughout
// Hipparcos/Gaia astrometry, e.g. ESA SP-1200 Vol 1, or Gaia DR3
// documentation section on epoch propagation).
constexpr double kAuYrPerKmS = 1.0 / 4.74047;

constexpr double kFallbackDistancePc = 3000.0;

} // namespace

SpaceMotion::Position SpaceMotion::propagate(const GaiaStar &star, double epochJyear)
{
    const double ra = star.raDeg * kDegToRad;
    const double dec = star.decDeg * kDegToRad;

    // Distance: from parallax when it's a usable (positive, non-negligible)
    // measurement, otherwise the same large fallback the Python prototype
    // uses -- see the header comment on why this doesn't matter much here.
    double distancePc = kFallbackDistancePc;
    if (star.hasParallax && star.parallaxMas > 0.1)
        distancePc = 1000.0 / star.parallaxMas;

    // Unit vector toward the star, and the local (East, North) tangent
    // basis at that position -- standard spherical-to-Cartesian setup.
    const Eigen::Vector3d u0(std::cos(dec) * std::cos(ra),
                              std::cos(dec) * std::sin(ra),
                              std::sin(dec));
    const Eigen::Vector3d eastHat(-std::sin(ra), std::cos(ra), 0.0);
    const Eigen::Vector3d northHat(-std::sin(dec) * std::cos(ra),
                                    -std::sin(dec) * std::sin(ra),
                                    std::cos(dec));

    const Eigen::Vector3d b0 = distancePc * u0; // position, pc

    // Tangential velocity from proper motion: angular rate (rad/yr) times
    // distance (pc) gives a linear rate in pc/yr directly (small-angle).
    const double pmraRadYr = star.pmraMasYr * kMasYrToRadYr;   // already *cos(dec)
    const double pmdecRadYr = star.pmdecMasYr * kMasYrToRadYr;
    const Eigen::Vector3d vTangentialPcYr =
        distancePc * (pmraRadYr * eastHat + pmdecRadYr * northHat);

    // Radial velocity contributes a component along u0. Convert km/s ->
    // pc/yr: 1 km/s = kAuYrPerKmS AU/yr, and 1 pc = 206264.806... AU.
    double vRadialPcYr = 0.0;
    if (star.hasRadialVelocity) {
        constexpr double kAuPerPc = 206264.80624709636;
        const double rvAuYr = star.radialVelocityKmS * kAuYrPerKmS;
        vRadialPcYr = rvAuYr / kAuPerPc;
    }

    const Eigen::Vector3d v = vTangentialPcYr + vRadialPcYr * u0; // pc/yr

    const double dt = epochJyear - star.refEpochJyear;
    const Eigen::Vector3d b = b0 + v * dt;

    const double r = b.norm();
    const Eigen::Vector3d u = (r > 0.0) ? (b / r) : u0;

    Position out;
    out.raDeg = std::atan2(u.y(), u.x()) * kRadToDeg;
    if (out.raDeg < 0.0)
        out.raDeg += 360.0;
    out.decDeg = std::asin(std::clamp(u.z(), -1.0, 1.0)) * kRadToDeg;
    return out;
}

} // namespace epochfrom
