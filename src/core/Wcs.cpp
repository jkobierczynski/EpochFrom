#include "Wcs.h"

#include <QFileInfo>

#include <fitsio.h>
#include <wcs.h>
#include <wcshdr.h>

#include <cstdlib>

namespace epochfrom {

namespace {

QString cfitsioErrorText(int status)
{
    char buf[FLEN_STATUS];
    fits_get_errstatus(status, buf);
    return QString::fromLocal8Bit(buf);
}

struct Holder {
    wcsprm *wcs;
    int nwcs;
};

} // namespace

Wcs::Wcs(const QString &wcsPath)
{
    if (!QFileInfo::exists(wcsPath)) {
        m_errorMessage = QStringLiteral("WCS file does not exist: %1").arg(wcsPath);
        return;
    }

    int status = 0;
    fitsfile *fptr = nullptr;
    if (fits_open_file(&fptr, wcsPath.toLocal8Bit().constData(), READONLY, &status) != 0) {
        m_errorMessage = QStringLiteral("failed to open WCS file: %1").arg(cfitsioErrorText(status));
        return;
    }

    char *header = nullptr;
    int nkeys = 0;
    if (fits_hdr2str(fptr, 0, nullptr, 0, &header, &nkeys, &status) != 0) {
        m_errorMessage =
            QStringLiteral("failed to extract FITS header: %1").arg(cfitsioErrorText(status));
        fits_close_file(fptr, &status);
        return;
    }
    fits_close_file(fptr, &status);

    int nreject = 0;
    int nwcs = 0;
    wcsprm *wcsHead = nullptr;
    const int pihStatus = wcspih(header, nkeys, WCSHDR_all, 0, &nreject, &nwcs, &wcsHead);
    // header was allocated by cfitsio; wcspih() copies what it needs.
    std::free(header);

    if (pihStatus != 0 || nwcs < 1 || wcsHead == nullptr) {
        m_errorMessage = QStringLiteral("wcslib failed to parse the WCS header (wcspih status %1)")
                              .arg(pihStatus);
        if (wcsHead)
            wcsvfree(&nwcs, &wcsHead);
        return;
    }

    // wcspih() hands back an array (nwcs structs); this only ever asks for
    // one WCS representation -- keep the whole array pointer and just use
    // element 0, freeing the array (not the struct) in the destructor via
    // wcsvfree(), same pattern LinearWcs uses.
    wcsprm *wcs = &wcsHead[0];
    const int setStatus = wcsset(wcs);
    if (setStatus != 0) {
        m_errorMessage = QStringLiteral("wcslib wcsset() failed (status %1)").arg(setStatus);
        wcsvfree(&nwcs, &wcsHead);
        return;
    }

    m_wcs = new Holder{wcsHead, nwcs};
    m_valid = true;
}

Wcs::~Wcs()
{
    if (m_wcs) {
        auto *holder = static_cast<Holder *>(m_wcs);
        wcsvfree(&holder->nwcs, &holder->wcs);
        delete holder;
    }
}

bool Wcs::pixToWorld(double pixX, double pixY, double *outRaDeg, double *outDecDeg) const
{
    if (!m_valid || !m_wcs)
        return false;

    auto *holder = static_cast<Holder *>(m_wcs);
    wcsprm *wcs = holder->wcs;

    const double pixcrd[2] = {pixX, pixY};
    double imgcrd[2] = {0.0, 0.0};
    double phi = 0.0, theta = 0.0;
    double world[2] = {0.0, 0.0};
    int stat = 0;
    const int status = wcsp2s(wcs, 1, 2, pixcrd, imgcrd, &phi, &theta, world, &stat);
    if (status != 0)
        return false;

    *outRaDeg = world[wcs->lng];
    *outDecDeg = world[wcs->lat];
    return true;
}

} // namespace epochfrom
