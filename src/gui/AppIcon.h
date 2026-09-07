#pragma once

// The EpochFrom app icon -- the same proper-motion glyph the Starfield tab
// itself draws (a star with a green "where it's heading" arrow and a red
// "where it came from" line, see StarfieldCanvas.cpp and
// docs/images/epochfrom-logo.svg for the source), baked into the binary at
// several resolutions via resources/icon.qrc so it works regardless of the
// binary's working directory -- unlike tools/residual-field.html (too big
// to embed this way), an icon is small enough that embedding it beats the
// candidate-relative-paths dance MainWindow::findResidualFieldViewer() has
// to do.

#include <QIcon>

namespace epochfrom::gui {

inline QIcon appIcon()
{
    QIcon icon;
    // Smallest-to-largest: QIcon picks the best match per requested size,
    // and this order also becomes the fallback preference if a platform
    // ever asks for a size none of these matches exactly.
    for (int size : {16, 24, 32, 48, 64, 128, 256, 512})
        icon.addFile(QStringLiteral(":/icons/epochfrom-%1.png").arg(size), QSize(size, size));
    return icon;
}

} // namespace epochfrom::gui
