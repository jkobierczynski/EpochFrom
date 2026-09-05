#pragma once

#include <QDir>
#include <QString>

namespace epochfrom::gui {

// Standard path layout under a project's base directory, so the user isn't
// stuck re-typing (and keeping in sync) a full set of paths by hand for
// every filter they process. Layout, given a base directory and an
// optional filter name (e.g. "Ha", "OIII", "SII"):
//
//   <base>/<subs dir pattern>/      subs for that filter, or <base>/ itself
//                                   if no filter is set -- not every rig
//                                   shoots narrowband/multi-filter subsets.
//                                   The subdirectory name is the pattern
//                                   with every "%filter%" replaced by the
//                                   filter name -- the default pattern is
//                                   just "%filter%" (so by default the
//                                   subdirectory IS the filter name, e.g.
//                                   <base>/Ha/), but a rig whose capture
//                                   software names session folders like
//                                   "Light_Ha_600_secs" can set the pattern
//                                   to "Light_%filter%_600_secs" instead of
//                                   renaming folders to match.
//   <base>/gaia.csv                Gaia DR3 field catalog, shared across
//                                   filters (the star field doesn't change
//                                   with filter, only how bright a star
//                                   looks through one)
//   <base>/profile.json             equipment profile, shared across
//                                   filters (the rig's own optical
//                                   distortion doesn't depend on filter)
//   <base>/<residuals filename      one calibration's residuals export,
//     pattern>                      named per filter (again via "%filter%"
//                                   substitution, default
//                                   "%filter%_residuals.csv") so
//                                   calibrating a second filter doesn't
//                                   overwrite the first one's residuals
//
// These are conveniences, not requirements: every field a "Fill from
// Project" button populates stays a plain, freely-editable path afterward,
// so nothing stops the user from pointing it elsewhere.

// Replaces every occurrence of the literal token "%filter%" in `pattern`
// with `filter`. Shared by every path below that accepts a pattern.
inline QString substituteFilterToken(const QString &pattern, const QString &filter)
{
    QString result = pattern;
    result.replace(QStringLiteral("%filter%"), filter);
    return result;
}

inline QString projectSubsDir(const QString &baseDir, const QString &filter,
                               const QString &dirPattern = QStringLiteral("%filter%"))
{
    const QString base = baseDir.trimmed();
    if (base.isEmpty())
        return QString();
    const QString f = filter.trimmed();
    if (f.isEmpty())
        return QDir::cleanPath(base); // no filter selected -- nothing to substitute, use base as-is
    const QString pattern =
        dirPattern.trimmed().isEmpty() ? QStringLiteral("%filter%") : dirPattern.trimmed();
    const QString subdirName = substituteFilterToken(pattern, f);
    return QDir::cleanPath(QDir(base).filePath(subdirName));
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

inline QString projectResidualsCsv(
    const QString &baseDir, const QString &filter,
    const QString &filenamePattern = QStringLiteral("%filter%_residuals.csv"))
{
    const QString base = baseDir.trimmed();
    if (base.isEmpty())
        return QString();
    const QString f = filter.trimmed();
    QString name;
    if (f.isEmpty()) {
        name = QStringLiteral("residuals.csv"); // no filter selected -- nothing to substitute
    } else {
        const QString pattern = filenamePattern.trimmed().isEmpty()
                                     ? QStringLiteral("%filter%_residuals.csv")
                                     : filenamePattern.trimmed();
        name = substituteFilterToken(pattern, f);
    }
    return QDir::cleanPath(QDir(base).filePath(name));
}

} // namespace epochfrom::gui
