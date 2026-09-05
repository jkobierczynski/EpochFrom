#pragma once

#include <QDir>
#include <QString>

namespace epochfrom::gui {

// Standard path layout under a project's base directory, so the user isn't
// stuck re-typing (and keeping in sync) a full set of paths by hand for
// every filter they process. Layout, given a base directory and an
// optional filter name (e.g. "Ha", "OIII", "SII"):
//
//   <base>/<filter>/               subs for that filter, or <base>/ itself
//                                   if no filter is set -- not every rig
//                                   shoots narrowband/multi-filter subsets
//   <base>/gaia.csv                Gaia DR3 field catalog, shared across
//                                   filters (the star field doesn't change
//                                   with filter, only how bright a star
//                                   looks through one)
//   <base>/profile.json             equipment profile, shared across
//                                   filters (the rig's own optical
//                                   distortion doesn't depend on filter)
//   <base>/<filter>_residuals.csv   one calibration's residuals export,
//                                   named per filter so calibrating a
//                                   second filter doesn't overwrite the
//                                   first one's residuals
//
// These are conveniences, not requirements: every field a "Fill from
// Project" button populates stays a plain, freely-editable path afterward,
// so nothing stops the user from pointing it elsewhere.

inline QString projectSubsDir(const QString &baseDir, const QString &filter)
{
    const QString base = baseDir.trimmed();
    if (base.isEmpty())
        return QString();
    const QString f = filter.trimmed();
    return f.isEmpty() ? QDir::cleanPath(base) : QDir::cleanPath(QDir(base).filePath(f));
}

inline QString projectGaiaCsv(const QString &baseDir)
{
    const QString base = baseDir.trimmed();
    if (base.isEmpty())
        return QString();
    return QDir::cleanPath(QDir(base).filePath(QStringLiteral("gaia.csv")));
}

inline QString projectProfilePath(const QString &baseDir)
{
    const QString base = baseDir.trimmed();
    if (base.isEmpty())
        return QString();
    return QDir::cleanPath(QDir(base).filePath(QStringLiteral("profile.json")));
}

inline QString projectResidualsCsv(const QString &baseDir, const QString &filter)
{
    const QString base = baseDir.trimmed();
    if (base.isEmpty())
        return QString();
    const QString f = filter.trimmed();
    const QString name = f.isEmpty() ? QStringLiteral("residuals.csv")
                                      : QStringLiteral("%1_residuals.csv").arg(f);
    return QDir::cleanPath(QDir(base).filePath(name));
}

} // namespace epochfrom::gui
