#include "FitsImage.h"

#include <QDate>
#include <QRegularExpression>

#include <fitsio.h>

namespace epochfrom {

namespace {

QString cfitsioErrorText(int status)
{
    char buf[FLEN_STATUS];
    fits_get_errstatus(status, buf);
    return QString::fromLocal8Bit(buf);
}

} // namespace

double FitsImage::parseDateObsToJyear(const QString &dateObs)
{
    // FITS DATE-OBS is "YYYY-MM-DDTHH:MM:SS[.sss...]" (always UTC in
    // practice for ground-based amateur captures) -- some capture software
    // (confirmed on a real file from this project's own prototyping
    // library) appends a trailing 'Z', which isn't standard FITS but is
    // harmless to accept. Parsed by hand with QDate + a regex for the time
    // part rather than QDateTime, to avoid relying on QDateTime's
    // TimeSpec/QTimeZone constructors at all (same reasoning as the CLI's
    // jyearToDateString).
    static const QRegularExpression re(
        QStringLiteral(R"(^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d+(?:\.\d+)?)Z?$)"));
    const QRegularExpressionMatch m = re.match(dateObs.trimmed());
    if (!m.hasMatch())
        return std::numeric_limits<double>::quiet_NaN();

    const int year = m.captured(1).toInt();
    const int month = m.captured(2).toInt();
    const int day = m.captured(3).toInt();
    const int hour = m.captured(4).toInt();
    const int minute = m.captured(5).toInt();
    const double second = m.captured(6).toDouble();

    const QDate date(year, month, day);
    if (!date.isValid())
        return std::numeric_limits<double>::quiet_NaN();

    // Julian Date: QDate::toJulianDay() gives the JD number for noon on
    // that calendar date (the standard integer-JDN convention, JD 2451545
    // for 2000-01-01) -- same relationship the CLI's jyearToDateString
    // relies on in reverse. Add the fractional day since midnight.
    const double dayFraction = (hour + minute / 60.0 + second / 3600.0) / 24.0;
    const double jd = static_cast<double>(date.toJulianDay()) - 0.5 + dayFraction;
    return 2000.0 + (jd - 2451545.0) / 365.25;
}

FitsImageData FitsImage::load(const QString &path)
{
    FitsImageData result;

    int status = 0;
    fitsfile *fptr = nullptr;
    if (fits_open_file(&fptr, path.toLocal8Bit().constData(), READONLY, &status) != 0) {
        result.errorMessage =
            QStringLiteral("failed to open FITS image: %1").arg(cfitsioErrorText(status));
        return result;
    }

    int naxis = 0;
    long naxes[2] = {0, 0};
    if (fits_get_img_dim(fptr, &naxis, &status) != 0 || naxis < 2 ||
        fits_get_img_size(fptr, 2, naxes, &status) != 0) {
        result.errorMessage = QStringLiteral("failed to read image dimensions: %1")
                                   .arg(cfitsioErrorText(status));
        fits_close_file(fptr, &status);
        return result;
    }

    const long width = naxes[0];
    const long height = naxes[1];
    if (width <= 0 || height <= 0) {
        result.errorMessage = QStringLiteral("image has no pixel data (NAXIS1/2 not positive)");
        fits_close_file(fptr, &status);
        return result;
    }

    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);
    result.pixels.resize(static_cast<int>(width * height));

    long firstpix[2] = {1, 1};
    int anynul = 0;
    // Read the whole image (all NAXIS1*NAXIS2 elements starting at pixel
    // (1,1)) as float, letting cfitsio apply BSCALE/BZERO for us -- reads
    // in file order, so pixels[i] for i = row*width + col lands exactly on
    // (row=FITS y-1, col=FITS x-1), matching FitsImageData's documented
    // convention.
    if (fits_read_pix(fptr, TFLOAT, firstpix, width * height, nullptr, result.pixels.data(),
                       &anynul, &status) != 0) {
        result.errorMessage =
            QStringLiteral("failed to read pixel data: %1").arg(cfitsioErrorText(status));
        fits_close_file(fptr, &status);
        return result;
    }

    char dateObsBuf[FLEN_VALUE];
    int keyStatus = 0;
    if (fits_read_key(fptr, TSTRING, "DATE-OBS", dateObsBuf, nullptr, &keyStatus) == 0) {
        result.dateObs = QString::fromLocal8Bit(dateObsBuf);
        result.dateObsJyear = parseDateObsToJyear(result.dateObs);
    }

    fits_close_file(fptr, &status);
    result.loaded = true;
    return result;
}

} // namespace epochfrom
