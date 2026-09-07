// Entry point for EpochFrom-starfield: a standalone window with nothing but
// the Project bar + Starfield tab from the main EpochFrom-gui app, for
// anyone who just wants the proper-motion viewer without the rest of the
// pipeline's tabs -- e.g. pinned open full-window while browsing a night's
// captures. See StarfieldTab.h for the tab itself, which this window is a
// thin wrapper around.

#include "AppIcon.h"
#include "ProjectBar.h"
#include "StarfieldTab.h"

#include <QApplication>
#include <QMainWindow>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Same organization + application name as EpochFrom-gui, deliberately:
    // this standalone viewer shares its QSettings with the full app's
    // Project bar (base directory, filter, Gaia CSV path, subs-dir/
    // residuals patterns), so whichever one you set up first, the other
    // already has it.
    QCoreApplication::setOrganizationName("EpochFrom");
    QCoreApplication::setApplicationName("EpochFrom-gui");
    QCoreApplication::setApplicationVersion("0.1.0");
    app.setWindowIcon(epochfrom::gui::appIcon());
    // Its own entry, distinct from EpochFrom-gui's -- see
    // packaging/linux/epochfrom-starfield.desktop -- even though the
    // shared applicationName above means the two still report the same
    // WM_CLASS to the window manager.
    QGuiApplication::setDesktopFileName(QStringLiteral("epochfrom-starfield"));

    QMainWindow window;
    window.setWindowTitle(QObject::tr("EpochFrom Starfield"));
    window.resize(1200, 900);

    auto *projectBar = new epochfrom::gui::ProjectBar;
    auto *starfieldTab = new epochfrom::gui::StarfieldTab(projectBar);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(projectBar);
    layout->addWidget(starfieldTab, 1);
    window.setCentralWidget(central);

    // The tab's own Fullscreen toggle (F11) already puts this window into
    // OS fullscreen and hides its own options panel; hide the project bar
    // too so fullscreen here really is just the starfield, full-window.
    QObject::connect(starfieldTab, &epochfrom::gui::StarfieldTab::fullscreenToggled, projectBar,
                      [projectBar](bool on) { projectBar->setVisible(!on); });

    window.show();
    return app.exec();
}
