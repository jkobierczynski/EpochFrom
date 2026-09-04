#pragma once

#include <QString>
#include <QVector>

namespace epochfrom {

// One row from a Gaia DR3 field query CSV, as produced by the tool's own
// query step (see scripts/gaia_field_query.py in this repo, ported from the
// Python prototype used to validate the math in this project). Field names
// mirror the Gaia archive's own column names so a catalog CSV round-trips
// without renaming.
struct GaiaStar {
    qint64 sourceId = 0;
    double refEpochJyear = 2016.0; // Gaia DR3 reference epoch, always 2016.0 today
    double raDeg = 0.0;
    double decDeg = 0.0;
    double pmraMasYr = 0.0;   // pmra*cos(dec) -- a true angular rate, not a raw coordinate rate
    double pmdecMasYr = 0.0;
    double parallaxMas = 0.0; // <= 0 or absent means "unmeasured" -- see SpaceMotion
    bool hasParallax = false;
    double radialVelocityKmS = 0.0;
    bool hasRadialVelocity = false;
    double photGMeanMag = 99.0;
    double ruwe = 0.0;
};

class GaiaCatalog {
public:
    // Loads a CSV in the format written by gaia_field_query.py: header row
    // with (at least) source_id, ref_epoch, ra, dec, pmra, pmdec, parallax,
    // radial_velocity, phot_g_mean_mag, ruwe. Extra columns are ignored.
    // Rows missing pmra/pmdec are skipped (unusable for space-motion
    // propagation, same filter the Python prototype applied).
    //
    // Returns false and sets *errorMessage (if non-null) on failure to open
    // or parse the file.
    static bool loadCsv(const QString &path, QVector<GaiaStar> *outStars,
                         QString *errorMessage = nullptr);
};

} // namespace epochfrom
