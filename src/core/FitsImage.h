#pragma once

#include <QString>
#include <QVector>
#include <limits>

namespace epochfrom {

// A loaded FITS image's pixel data plus the handful of header keywords the
// calibration pipeline needs. Pixel data is stored exactly as it appears in
// the file: pixels[row * width + col], row 0 = the first row written to the
// file (FITS pixel y=1), col 0 = FITS pixel x=1 -- the same convention
// astropy/numpy's fits.getdata() uses, so centroids computed against this
// buffer land in the same coordinate system the Python prototype's
// DAOStarFinder-based centroids did (0-indexed; add 1 to get a FITS/WCS
// pixel coordinate).
struct FitsImageData {
    bool loaded = false;
    int width = 0;
    int height = 0;
    QVector<float> pixels;
    QString dateObs;                                                  // raw DATE-OBS string, if present
    double dateObsJyear = std::numeric_limits<double>::quiet_NaN();  // parsed, or NaN if absent/unparseable
    QString errorMessage;                                             // set when loaded == false
};

class FitsImage {
public:
    static FitsImageData load(const QString &path);

    // Exposed for testing / reuse: Julian-year conversion of a FITS
    // DATE-OBS string ("YYYY-MM-DDTHH:MM:SS[.sss]", assumed UTC). Uses pure
    // QDate arithmetic (no QDateTime timezone constructor -- same reasoning
    // as the CLI's jyearToDateString) and the project's existing
    // 365.25-day Julian-year convention (JD 2451545.0 = jyear 2000.0).
    // Returns NaN if the string can't be parsed.
    static double parseDateObsToJyear(const QString &dateObs);
};

} // namespace epochfrom
