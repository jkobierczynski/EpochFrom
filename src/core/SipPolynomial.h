#pragma once

// Shared polynomial term definitions for the equipment-profiling SIP-style
// distortion fit -- used both when building the least-squares design matrix
// (EquipmentCalibrator) and when evaluating a saved profile's fitted
// polynomial at a single point (EquipmentProfile::evaluateCorrectionMas).
// Kept in one header so the two can never silently disagree on term order.
//
// Direct port of the Python prototype's poly_terms(): a full 2-variable
// polynomial up to `order`, terms u^p * v^q for all p,q >= 0 with
// p + q <= order, in row-major (p outer, q inner) order. u/v are pixel
// offsets from CRPIX, pre-normalized by dividing by a project-wide
// normalization constant (roughly half the sensor's long axis) -- fitting
// against raw, un-normalized pixel-power terms above ~order 3 was found to
// be severely ill-conditioned on a 4000+ px sensor during prototyping (see
// docs/equipment-profiling-spec.md section 4).

namespace epochfrom {

inline int sipPolyTermCount(int order)
{
    return (order + 1) * (order + 2) / 2;
}

// Appends the (order+1)(order+2)/2 terms for one (u, v) sample -- u, v
// already normalized -- to `outTerms` in the fixed p-outer/q-inner order.
template <typename Container>
inline void sipPolyTermsInto(double uNorm, double vNorm, int order, Container &outTerms)
{
    for (int p = 0; p <= order; ++p) {
        double up = 1.0;
        for (int i = 0; i < p; ++i)
            up *= uNorm;
        for (int q = 0; q <= order - p; ++q) {
            double vq = 1.0;
            for (int i = 0; i < q; ++i)
                vq *= vNorm;
            outTerms.push_back(up * vq);
        }
    }
}

} // namespace epochfrom
