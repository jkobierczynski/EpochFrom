// Unit test for combineDateEstimates() -- the inverse-variance-weighted
// average that turns a batch `date --dir` run's per-image epoch estimates
// into a single combined date (see ReportFormatting.h). Pure math over
// hand-built DateEstimateResult values, no FITS/Gaia/WCS fixtures needed.

#include "ReportFormatting.h"

#include <QCoreApplication>
#include <QTextStream>

#include <cmath>
#include <limits>

using namespace epochfrom;

namespace {

bool nearlyEqual(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

DateEstimateResult makeResult(double epochJyear, double epochSigmaYears, bool ok = true,
                               bool converged = true, bool rankDeficient = false)
{
    DateEstimateResult r;
    r.ok = ok;
    r.epochJyear = epochJyear;
    r.epochSigmaYears = epochSigmaYears;
    r.converged = converged;
    r.rankDeficient = rankDeficient;
    return r;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    bool ok = true;

    // Case 1: two equally-precise estimates straddling a known epoch ->
    // combined epoch should land exactly at the midpoint, and the combined
    // sigma should be tighter than either individual one (1/sqrt(2) of it,
    // for equal weights).
    {
        QVector<DateEstimateResult> results;
        results.append(makeResult(2020.0, 0.01));
        results.append(makeResult(2020.5, 0.01));
        const CombinedDateEstimate combined = combineDateEstimates(results);
        if (!combined.ok || combined.nCombined != 2 || combined.nExcluded != 0) {
            out << "FAIL: case 1 -- expected ok, nCombined=2, nExcluded=0\n";
            ok = false;
        }
        if (!nearlyEqual(combined.epochJyear, 2020.25, 1e-9)) {
            out << QString("FAIL: case 1 -- expected epoch 2020.25, got %1\n")
                       .arg(combined.epochJyear, 0, 'f', 9);
            ok = false;
        }
        const double expectedSigma = 0.01 / std::sqrt(2.0);
        if (!nearlyEqual(combined.epochSigmaYears, expectedSigma, 1e-9)) {
            out << QString("FAIL: case 1 -- expected sigma %1, got %2\n")
                       .arg(expectedSigma, 0, 'f', 9)
                       .arg(combined.epochSigmaYears, 0, 'f', 9);
            ok = false;
        }
    }

    // Case 2: a tightly-constrained estimate should pull the combined date
    // toward itself far more than a loose one -- not a plain unweighted
    // average of the two epochs (which would land at 2020.25).
    {
        QVector<DateEstimateResult> results;
        results.append(makeResult(2020.0, 0.001)); // precise
        results.append(makeResult(2020.5, 0.1));    // loose
        const CombinedDateEstimate combined = combineDateEstimates(results);
        if (!combined.ok) {
            out << "FAIL: case 2 -- expected ok\n";
            ok = false;
        }
        if (!(combined.epochJyear < 2020.01)) {
            out << QString("FAIL: case 2 -- expected combined epoch close to the precise "
                            "estimate (2020.0), got %1\n")
                       .arg(combined.epochJyear, 0, 'f', 6);
            ok = false;
        }
    }

    // Case 3: a failed (ok=false), a non-converged, a rank-deficient, and a
    // non-finite-sigma result should all be excluded rather than poisoning
    // or dominating the average -- only the one good result should combine.
    {
        QVector<DateEstimateResult> results;
        results.append(makeResult(1999.0, 0.5, /*ok=*/false));
        results.append(makeResult(1999.0, 0.001, /*ok=*/true, /*converged=*/false));
        results.append(makeResult(1999.0, 0.001, /*ok=*/true, /*converged=*/true,
                                   /*rankDeficient=*/true));
        results.append(makeResult(1999.0, std::numeric_limits<double>::quiet_NaN()));
        results.append(makeResult(2021.0, 0.02));
        const CombinedDateEstimate combined = combineDateEstimates(results);
        if (!combined.ok || combined.nCombined != 1 || combined.nExcluded != 3) {
            out << QString("FAIL: case 3 -- expected ok, nCombined=1, nExcluded=3, got "
                            "ok=%1 nCombined=%2 nExcluded=%3\n")
                       .arg(combined.ok)
                       .arg(combined.nCombined)
                       .arg(combined.nExcluded);
            ok = false;
        }
        if (!nearlyEqual(combined.epochJyear, 2021.0, 1e-9)) {
            out << QString("FAIL: case 3 -- expected epoch 2021.0 (only the good result), got %1\n")
                       .arg(combined.epochJyear, 0, 'f', 9);
            ok = false;
        }
    }

    // Case 4: no usable results at all -> ok stays false.
    {
        QVector<DateEstimateResult> results;
        results.append(makeResult(2020.0, 0.0, /*ok=*/false));
        const CombinedDateEstimate combined = combineDateEstimates(results);
        if (combined.ok) {
            out << "FAIL: case 4 -- expected not ok with no usable results\n";
            ok = false;
        }
    }
    {
        QVector<DateEstimateResult> empty;
        const CombinedDateEstimate combined = combineDateEstimates(empty);
        if (combined.ok) {
            out << "FAIL: case 4b -- expected not ok for an empty batch\n";
            ok = false;
        }
    }

    out << (ok ? "\nRESULT: PASS\n" : "\nRESULT: FAIL\n");
    return ok ? 0 : 1;
}
