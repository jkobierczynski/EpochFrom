#pragma once

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace epochfrom::gui {

// Same convention the CLI uses everywhere (src/cli/main.cpp): a sub's WCS
// sidecar sits next to it, same base name, ".wcs" extension. Kept here as
// one shared helper so the three GUI tabs (which each need to batch over a
// directory the same way `solve --dir`/`calibrate --dir`/`date --dir` do)
// can't drift from each other or from the CLI on this.
inline QStringList listFitsFiles(const QDir &dir)
{
    return dir.entryList({"*.fits", "*.fit", "*.fts"}, QDir::Files, QDir::Name);
}

inline QString wcsSidecarPath(const QFileInfo &fitsFile)
{
    return fitsFile.dir().filePath(fitsFile.completeBaseName() + ".wcs");
}

} // namespace epochfrom::gui
