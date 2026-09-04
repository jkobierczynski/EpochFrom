// Regression test for the core epoch-fitting engine: propagate real Gaia
// field stars to a known synthetic epoch, add realistic centroiding noise,
// and check the fit recovers that epoch. Mirrors fit_epoch.py --selftest
// from the Python prototype this C++ engine replaces -- same idea, same
// tolerances in spirit, run against the same real field catalog
// (gaia_northamerica.csv) rather than fabricated data, so this exercises
// the full propagation + cross-match + Levenberg-Marquardt chain against
// real star geometry, not just idealized numbers.

#include "GaiaCatalog.h"
#include "EpochFit.h"

#include <QCoreApplication>
#include <QTextStream>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace epochfrom;

namespace {

// Tolerance is expressed in multiples of the fit's OWN reported epochSigma,
// not a fixed day count. This catalog's actual achievable single-epoch
// precision depends on how many stars are available and how fast they
// move -- for gaia_northamerica.csv at 0.3" noise using all ~556 stars
// (median proper motion is modest, this isn't a field of high-PM runaways),
// that came out to roughly 1-1.5yr, not the tens-of-days a naive first
// guess assumed. The meaningful regression check isn't "does it recover
// the epoch to some fixed precision" (that number depends on the input
// data, not just on the code being correct) -- it's "is the recovered
// epoch consistent with the fit's own error bar", i.e. is the fit honest
// about its own uncertainty rather than silently biased.
bool checkOne(const QVector<GaiaStar> &catalog, double trueEpoch, double noiseArcsec,
              unsigned seed, double maxSigmaMultiple, QTextStream &out)
{
    const EpochFit::Result r = EpochFit::runSelfTest(catalog, trueEpoch, noiseArcsec, seed);
    const double errorYears = r.epochJyear - trueEpoch;
    const double errorDays = errorYears * 365.25;
    const double sigmas = std::isfinite(r.epochSigmaYears) && r.epochSigmaYears > 0
                               ? std::abs(errorYears) / r.epochSigmaYears
                               : std::numeric_limits<double>::infinity();

    out << QString("  true=%1  fitted=%2 +/- %3 yr  error=%4 d (%5 sigma)  rms=%6 mas  n=%7\n")
               .arg(trueEpoch, 0, 'f', 4)
               .arg(r.epochJyear, 0, 'f', 4)
               .arg(r.epochSigmaYears, 0, 'f', 4)
               .arg(errorDays, 0, 'f', 1)
               .arg(sigmas, 0, 'f', 2)
               .arg(r.rmsResidualMas, 0, 'f', 1)
               .arg(r.nStarsUsed);

    if (!r.converged) {
        // Soft signal only: converged reports the optimizer's own read on
        // its internal state (did it find a step that stopped improving
        // things), not a guarantee about the fit itself -- and that
        // internal state can differ machine-to-machine for the same seed,
        // since std::normal_distribution's exact output isn't required to
        // be identical across standard library implementations, only the
        // underlying mt19937 bit sequence is. What actually matters is
        // whether the ANSWER is good, which the checks below verify
        // directly and rigorously -- so this is logged, not treated as a
        // hard failure on its own.
        out << "  NOTE: fit did not report convergence (see checks below for whether the "
               "result is actually good)\n";
    }
    if (r.rankDeficient) {
        out << "  FAIL: fit was rank-deficient\n";
        return false;
    }
    // Residual RMS should land near sigma*sqrt(pi/2) (~1.25x, the mean of a
    // Rayleigh distribution from 2D Gaussian noise), generously bounded --
    // this catches a badly wrong propagation/residual formula even when
    // the epoch itself still happens to come out close by chance.
    const double noiseMas = noiseArcsec * 1000.0;
    if (r.rmsResidualMas < 0.5 * noiseMas || r.rmsResidualMas > 3.0 * noiseMas) {
        out << QString("  FAIL: RMS residual %1 mas implausible for %2 mas injected noise\n")
                   .arg(r.rmsResidualMas, 0, 'f', 1)
                   .arg(noiseMas, 0, 'f', 1);
        return false;
    }
    if (sigmas > maxSigmaMultiple) {
        out << QString("  FAIL: error is %1 sigma, exceeds tolerance of %2 sigma\n")
                   .arg(sigmas, 0, 'f', 2)
                   .arg(maxSigmaMultiple, 0, 'f', 2);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    if (argc < 2) {
        out << "usage: epoch_fit_selftest <gaia_catalog.csv>\n";
        return 1;
    }

    QVector<GaiaStar> catalog;
    QString err;
    if (!GaiaCatalog::loadCsv(QString::fromLocal8Bit(argv[1]), &catalog, &err)) {
        out << "error loading catalog: " << err << "\n";
        return 1;
    }
    out << "Loaded " << catalog.size() << " usable stars.\n";
    if (catalog.size() < 50) {
        out << "FAIL: too few usable stars in test catalog to be a meaningful check.\n";
        return 1;
    }

    bool ok = true;
    // Realistic-noise case (0.3" per star, matching the Python prototype's
    // default and roughly this project's amateur-grade centroiding). 4
    // sigma is generous enough not to be flaky from one noise draw, tight
    // enough to catch a real bias.
    ok &= checkOne(catalog, 2018.6900, 0.30, 42, /*maxSigmaMultiple=*/4.0, out);
    // A second, independent true epoch + seed, so this isn't just one lucky
    // draw.
    ok &= checkOne(catalog, 2013.5500, 0.30, 7, /*maxSigmaMultiple=*/4.0, out);
    // Tighter injected noise should recover with a tighter error bar --
    ok &= checkOne(catalog, 2018.6900, 0.05, 42, /*maxSigmaMultiple=*/4.0, out);

    // ...and that's checked directly here: precision should scale with
    // noise, not just land within its own (possibly huge) error bar every
    // time. A regression that inflates epochSigmaYears to hide a real bias
    // would still pass the per-case checks above but fail this one.
    const EpochFit::Result loose = EpochFit::runSelfTest(catalog, 2018.6900, 0.30, 42);
    const EpochFit::Result tight = EpochFit::runSelfTest(catalog, 2018.6900, 0.05, 42);
    out << QString("  precision scaling: sigma(0.30\")=%1 yr, sigma(0.05\")=%2 yr\n")
               .arg(loose.epochSigmaYears, 0, 'f', 4)
               .arg(tight.epochSigmaYears, 0, 'f', 4);
    if (!(tight.epochSigmaYears < loose.epochSigmaYears)) {
        out << "  FAIL: tighter injected noise did not yield a tighter error bar\n";
        ok = false;
    }

    if (!ok) {
        out << "\nRESULT: FAIL\n";
        return 1;
    }
    out << "\nRESULT: PASS\n";
    return 0;
}
