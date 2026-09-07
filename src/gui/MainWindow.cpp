#include "MainWindow.h"
#include "CalibrateTab.h"
#include "DateTab.h"
#include "GaiaTab.h"
#include "ProjectBar.h"
#include "SolveTab.h"
#include "StarfieldTab.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace epochfrom::gui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("EpochFrom"));
    resize(900, 720);

    // The project bar sits above the tabs and holds settings shared by all
    // of them (base directory + filter) -- see ProjectBar.h. Each tab pulls
    // from it on demand via its own "Fill from Project" button.
    auto *projectBar = new ProjectBar;

    auto *tabs = new QTabWidget;
    auto *solveTab = new SolveTab(projectBar);
    auto *gaiaTab = new GaiaTab(projectBar);
    auto *calibrateTab = new CalibrateTab(projectBar);
    auto *dateTab = new DateTab(projectBar);
    auto *starfieldTab = new StarfieldTab(projectBar);
    tabs->addTab(solveTab, tr("Solve"));
    tabs->addTab(gaiaTab, tr("Gaia"));
    tabs->addTab(calibrateTab, tr("Calibrate"));
    tabs->addTab(dateTab, tr("Date"));
    tabs->addTab(starfieldTab, tr("Starfield"));

    // The Project bar's "Fill All Tabs" button composes every tab's paths
    // in one click, instead of clicking each tab's own "Fill from Project"
    // button in turn.
    connect(projectBar, &ProjectBar::fillAllRequested, solveTab, &SolveTab::fillFromProject);
    connect(projectBar, &ProjectBar::fillAllRequested, gaiaTab, &GaiaTab::fillFromProject);
    connect(projectBar, &ProjectBar::fillAllRequested, calibrateTab, &CalibrateTab::fillFromProject);
    connect(projectBar, &ProjectBar::fillAllRequested, dateTab, &DateTab::fillFromProject);
    connect(projectBar, &ProjectBar::fillAllRequested, starfieldTab, &StarfieldTab::fillFromProject);

    // The Starfield tab's own Fullscreen toggle (F11) puts this whole
    // window into OS fullscreen and hides its own options panel; it can't
    // reach this window's project bar/tab strip/menu/status bar itself, so
    // it emits a signal for exactly this instead.
    connect(starfieldTab, &StarfieldTab::fullscreenToggled, this, [this, projectBar, tabs](bool on) {
        projectBar->setVisible(!on);
        tabs->tabBar()->setVisible(!on);
        menuBar()->setVisible(!on);
        statusBar()->setVisible(!on);
    });

    auto *central = new QWidget;
    auto *centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(4, 4, 4, 4);
    centralLayout->addWidget(projectBar);
    centralLayout->addWidget(tabs, 1);
    setCentralWidget(central);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    QAction *residualAction = toolsMenu->addAction(tr("Open Residual Field Viewer..."));
    connect(residualAction, &QAction::triggered, this, &MainWindow::openResidualFieldViewer);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("About EpochFrom"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    statusBar()->showMessage(
        tr("Gaia (once, per field), then Solve, then Calibrate (once, per rig), then Date -- set "
           "a base directory above, then \"Fill All Tabs\" to fill in every tab's paths in one "
           "click."));
}

QString MainWindow::findResidualFieldViewer() const
{
    // tools/residual-field.html isn't installed anywhere fixed yet (there's
    // no `install` target in this early-scaffold project) -- so look in the
    // handful of places it plausibly sits relative to wherever this binary
    // happens to be running from, both in a build tree and run straight out
    // of a checkout.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("tools/residual-field.html"),
        QDir(appDir).filePath("../tools/residual-field.html"),
        QDir(appDir).filePath("../../tools/residual-field.html"),
        QDir(appDir).filePath("../../../tools/residual-field.html"),
        QDir::current().filePath("tools/residual-field.html"),
    };
    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (QFileInfo::exists(cleaned))
            return cleaned;
    }
    return QString();
}

void MainWindow::openResidualFieldViewer()
{
    const QString path = findResidualFieldViewer();
    if (path.isEmpty()) {
        QMessageBox::information(
            this, tr("Residual Field Viewer"),
            tr("Couldn't find tools/residual-field.html automatically. It lives in this "
               "project's repository root, under tools/ -- open it directly in a browser, then "
               "load a residuals CSV from the Calibrate tab into it."));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("About EpochFrom"),
        tr("<b>EpochFrom</b><br>Determines the capture date of astrophotography images by "
           "plate-solving frames and fitting Gaia DR3 stellar proper motions against the "
           "observed star positions, and profiles a telescope/camera rig's own optical "
           "distortion so it can be corrected rather than mistaken for proper-motion signal."
           "<br><br>This window wraps the same core engine as the <code>EpochFrom</code> "
           "command-line tool, plus the Gaia catalog downloader (scripts/gaia_field_query.py) -- "
           "see the project README for the full pipeline this automates. The Project bar above "
           "the tabs holds a base directory and filter shared by every tab's \"Fill from "
           "Project\" button -- or click the Project bar's own \"Fill All Tabs\" to fill in "
           "every tab at once."));
}

} // namespace epochfrom::gui
