#include "MainWindow.h"
#include "CalibrateTab.h"
#include "DateTab.h"
#include "SolveTab.h"

#include <QAction>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QUrl>

namespace epochfrom::gui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("EpochFrom"));
    resize(900, 720);

    auto *tabs = new QTabWidget;
    tabs->addTab(new SolveTab, tr("Solve"));
    tabs->addTab(new CalibrateTab, tr("Calibrate"));
    tabs->addTab(new DateTab, tr("Date"));
    setCentralWidget(tabs);

    auto *toolsMenu = menuBar()->addMenu(tr("&Tools"));
    QAction *residualAction = toolsMenu->addAction(tr("Open Residual Field Viewer..."));
    connect(residualAction, &QAction::triggered, this, &MainWindow::openResidualFieldViewer);

    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction *aboutAction = helpMenu->addAction(tr("About EpochFrom"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);

    statusBar()->showMessage(
        tr("Solve, then Calibrate (once, per rig), then Date -- see each tab's fields for what "
           "they need."));
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
           "command-line tool -- see the project README for the full pipeline this automates."));
}

} // namespace epochfrom::gui
