#include "AppIcon.h"
#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EpochFrom");
    QCoreApplication::setApplicationName("EpochFrom-gui");
    QCoreApplication::setApplicationVersion("0.1.0");
    app.setWindowIcon(epochfrom::gui::appIcon());
    // Matches packaging/linux/epochfrom-gui.desktop's own basename -- lets
    // GNOME/Wayland (and anything else that keys off this rather than
    // WM_CLASS) find that installed entry, including its Icon=, once it's
    // actually installed (see src/gui/CMakeLists.txt's install() rules).
    QGuiApplication::setDesktopFileName(QStringLiteral("epochfrom-gui"));

    epochfrom::gui::MainWindow window;
    window.show();

    return app.exec();
}
