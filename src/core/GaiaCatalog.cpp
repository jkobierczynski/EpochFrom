#include "GaiaCatalog.h"

#include <QFile>
#include <QTextStream>
#include <QHash>
#include <cmath>
#include <limits>

namespace epochfrom {

static double toDoubleOrNan(const QString &s)
{
    bool ok = false;
    double v = s.toDouble(&ok);
    return ok ? v : std::numeric_limits<double>::quiet_NaN();
}

bool GaiaCatalog::loadCsv(const QString &path, QVector<GaiaStar> *outStars, QString *errorMessage)
{
    Q_ASSERT(outStars);
    outStars->clear();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not open '%1' for reading.").arg(path);
        return false;
    }

    QTextStream in(&file);
    const QString headerLine = in.readLine();
    if (headerLine.isNull()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("'%1' is empty.").arg(path);
        return false;
    }

    const QStringList headers = headerLine.split(',');
    QHash<QString, int> col;
    for (int i = 0; i < headers.size(); ++i)
        col.insert(headers[i].trimmed(), i);

    // These are the columns the fit actually needs; everything else
    // (ra_error, dec_error, phot_bp/rp_mean_mag, ...) is read straight
    // through from the CSV but not required here.
    const QStringList required = {"source_id", "ref_epoch", "ra", "dec", "pmra", "pmdec"};
    for (const QString &key : required) {
        if (!col.contains(key)) {
            if (errorMessage)
                *errorMessage = QStringLiteral("'%1' is missing required column '%2'.").arg(path, key);
            return false;
        }
    }

    auto field = [&](const QStringList &parts, const QString &name) -> QString {
        const int idx = col.value(name, -1);
        if (idx < 0 || idx >= parts.size())
            return QString();
        return parts[idx];
    };

    int lineNo = 1;
    int skipped = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        ++lineNo;
        if (line.trimmed().isEmpty())
            continue;

        // NOTE: naive split on ',' -- correct for gaia_field_query.py's own
        // output (it writes plain numeric CSV via astropy Table.write, no
        // quoted/embedded commas), but not a general-purpose CSV parser.
        const QStringList parts = line.split(',');

        GaiaStar star;
        star.sourceId = field(parts, "source_id").toLongLong();
        star.refEpochJyear = toDoubleOrNan(field(parts, "ref_epoch"));
        star.raDeg = toDoubleOrNan(field(parts, "ra"));
        star.decDeg = toDoubleOrNan(field(parts, "dec"));
        star.pmraMasYr = toDoubleOrNan(field(parts, "pmra"));
        star.pmdecMasYr = toDoubleOrNan(field(parts, "pmdec"));

        if (std::isnan(star.pmraMasYr) || std::isnan(star.pmdecMasYr)) {
            // Same filter the Python prototype applies: a star with no
            // measured proper motion can't be propagated, so it's useless
            // for epoch fitting.
            ++skipped;
            continue;
        }

        if (col.contains("parallax")) {
            const double plx = toDoubleOrNan(field(parts, "parallax"));
            if (!std::isnan(plx)) {
                star.parallaxMas = plx;
                star.hasParallax = true;
            }
        }
        if (col.contains("radial_velocity")) {
            const double rv = toDoubleOrNan(field(parts, "radial_velocity"));
            if (!std::isnan(rv)) {
                star.radialVelocityKmS = rv;
                star.hasRadialVelocity = true;
            }
        }
        if (col.contains("phot_g_mean_mag"))
            star.photGMeanMag = toDoubleOrNan(field(parts, "phot_g_mean_mag"));
        if (col.contains("ruwe"))
            star.ruwe = toDoubleOrNan(field(parts, "ruwe"));

        outStars->push_back(star);
    }

    Q_UNUSED(lineNo);
    Q_UNUSED(skipped);
    return true;
}

} // namespace epochfrom
