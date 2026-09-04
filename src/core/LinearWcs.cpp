#include "LinearWcs.h"

#include <wcs.h>
#include <wcshdr.h>

#include <cstdio>
#include <cstring>

namespace epochfrom {

namespace {

// Formats one 80-column FITS header card for a floating-point value, using
// the standard fixed-format layout (keyword in cols 1-8, '=' in col 9,
// value right-justified in a wide field) -- doesn't need to be pretty, just
// needs to be something wcslib's parser accepts, and matching the standard
// layout is the safest way to guarantee that.
QByteArray fitsCardDouble(const char *keyword, double value)
{
    char buf[81];
    // %-8s= %20.13E leaves room for a full-precision scientific value;
    // wcslib's Fortran-derived free-format numeric parser accepts this
    // without trouble.
    std::snprintf(buf, sizeof(buf), "%-8s= %20.13E", keyword, value);
    QByteArray card(buf);
    if (card.size() < 80)
        card += QByteArray(80 - card.size(), ' ');
    return card.left(80);
}

QByteArray fitsCardString(const char *keyword, const char *value)
{
    char buf[81];
    std::snprintf(buf, sizeof(buf), "%-8s= '%-8s'", keyword, value);
    QByteArray card(buf);
    if (card.size() < 80)
        card += QByteArray(80 - card.size(), ' ');
    return card.left(80);
}

QByteArray fitsCardInt(const char *keyword, int value)
{
    char buf[81];
    std::snprintf(buf, sizeof(buf), "%-8s= %20d", keyword, value);
    QByteArray card(buf);
    if (card.size() < 80)
        card += QByteArray(80 - card.size(), ' ');
    return card.left(80);
}

} // namespace

LinearWcs::LinearWcs(double crval1Deg, double crval2Deg, double crpix1, double crpix2, double cd11,
                       double cd12, double cd21, double cd22)
{
    QByteArray header;
    header += fitsCardString("SIMPLE", "T");
    header += fitsCardInt("BITPIX", 16);
    header += fitsCardInt("NAXIS", 0);
    header += fitsCardString("CTYPE1", "RA---TAN");
    header += fitsCardString("CTYPE2", "DEC--TAN");
    header += fitsCardString("CUNIT1", "deg");
    header += fitsCardString("CUNIT2", "deg");
    header += fitsCardDouble("CRVAL1", crval1Deg);
    header += fitsCardDouble("CRVAL2", crval2Deg);
    header += fitsCardDouble("CRPIX1", crpix1);
    header += fitsCardDouble("CRPIX2", crpix2);
    header += fitsCardDouble("CD1_1", cd11);
    header += fitsCardDouble("CD1_2", cd12);
    header += fitsCardDouble("CD2_1", cd21);
    header += fitsCardDouble("CD2_2", cd22);
    header += QByteArray("END").leftJustified(80, ' ');
    const int nkeyrec = header.size() / 80;

    int nreject = 0;
    int nwcs = 0;
    wcsprm *wcsHead = nullptr;
    const int pihStatus = wcspih(header.data(), nkeyrec, WCSHDR_all, 0, &nreject, &nwcs, &wcsHead);
    if (pihStatus != 0 || nwcs < 1 || wcsHead == nullptr) {
        m_errorMessage = QStringLiteral("wcslib failed to parse synthetic linear header "
                                          "(wcspih status %1)")
                              .arg(pihStatus);
        if (wcsHead)
            wcsvfree(&nwcs, &wcsHead);
        return;
    }

    // wcspih() hands back an array (nwcs structs); we only ever ask for one
    // WCS representation, so take the first and detach it from that array
    // with wcssub-style extraction isn't necessary here -- simplest is to
    // copy the single struct out and free the array shell around it. wcslib
    // doesn't provide a clean "take ownership of one element" API, so
    // instead we keep the whole array pointer and just use element 0,
    // freeing the array (not the struct) in the destructor via wcsvfree.
    wcsprm *wcs = &wcsHead[0];
    const int setStatus = wcsset(wcs);
    if (setStatus != 0) {
        m_errorMessage = QStringLiteral("wcslib wcsset() failed (status %1)").arg(setStatus);
        wcsvfree(&nwcs, &wcsHead);
        return;
    }

    // Stash nwcs alongside the pointer so the destructor can call
    // wcsvfree() correctly; simplest is a tiny heap struct.
    struct Holder {
        wcsprm *wcs;
        int nwcs;
    };
    m_wcs = new Holder{wcsHead, nwcs};
    m_valid = true;
}

LinearWcs::~LinearWcs()
{
    if (m_wcs) {
        struct Holder {
            wcsprm *wcs;
            int nwcs;
        };
        auto *holder = static_cast<Holder *>(m_wcs);
        wcsvfree(&holder->nwcs, &holder->wcs);
        delete holder;
    }
}

bool LinearWcs::pixToWorld(double pixX, double pixY, double *outRaDeg, double *outDecDeg) const
{
    if (!m_valid || !m_wcs)
        return false;

    struct Holder {
        wcsprm *wcs;
        int nwcs;
    };
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
