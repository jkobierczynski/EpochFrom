#include "Wcs.h"

#include <QFileInfo>

#include <QStringList>

#include <dis.h>
#include <fitsio.h>
#include <wcs.h>
#include <wcserr.h>
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

// wcslib only populates a struct wcsprm's ->err->msg with a specific,
// human-readable explanation (naming the offending keyword/value, not just
// a numeric status) once wcserr_enable(1) has been called -- off by
// default. Harmless and idempotent to call repeatedly; done right before
// the wcslib calls below so any failure from here on carries real detail.
// A SIP/TPV/etc. distortion failure can originate one level down, in the
// disprm struct wcsprm::lin::dispre (prior distortion) or ::disseq
// (sequent) points at -- wcs->err->msg alone was observed (CI, wcslib 8.9)
// to carry only the generic "Invalid parameter value" text for exactly
// this kind of failure, so check those too and report whichever messages
// are actually populated.
QString wcsErrorDetail(const wcsprm *wcs)
{
    if (!wcs)
        return QString();
    QStringList details;
    if (wcs->err && wcs->err->msg && wcs->err->msg[0] != '\0')
        details << QString::fromLocal8Bit(wcs->err->msg);
    if (wcs->lin.dispre && wcs->lin.dispre->err && wcs->lin.dispre->err->msg &&
        wcs->lin.dispre->err->msg[0] != '\0') {
        details << QStringLiteral("prior distortion: %1")
                       .arg(QString::fromLocal8Bit(wcs->lin.dispre->err->msg));
    }
    if (wcs->lin.disseq && wcs->lin.disseq->err && wcs->lin.disseq->err->msg &&
        wcs->lin.disseq->err->msg[0] != '\0') {
        details << QStringLiteral("sequent distortion: %1")
                       .arg(QString::fromLocal8Bit(wcs->lin.disseq->err->msg));
    }
    if (details.isEmpty())
        return QString();
    return QStringLiteral(": %1").arg(details.join(QStringLiteral(" / ")));
}

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

    wcserr_enable(1);

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
        m_errorMessage = QStringLiteral("wcslib wcsset() failed (status %1)%2")
                              .arg(setStatus)
                              .arg(wcsErrorDetail(wcs));
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

bool Wcs::worldToPix(double raDeg, double decDeg, double *outPixX, double *outPixY) const
{
    if (!m_valid || !m_wcs)
        return false;

    auto *holder = static_cast<Holder *>(m_wcs);
    wcsprm *wcs = holder->wcs;

    double world[2] = {0.0, 0.0};
    world[wcs->lng] = raDeg;
    world[wcs->lat] = decDeg;
    double phi = 0.0, theta = 0.0;
    double imgcrd[2] = {0.0, 0.0};
    double pixcrd[2] = {0.0, 0.0};
    int stat = 0;
    const int status = wcss2p(wcs, 1, 2, world, &phi, &theta, imgcrd, pixcrd, &stat);
    if (status != 0)
        return false;

    *outPixX = pixcrd[0];
    *outPixY = pixcrd[1];
    return true;
}

} // namespace epochfrom
