#pragma once

#include "StarfieldWorker.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QThread;

namespace epochfrom::gui {

class NoWheelDoubleSpinBox;
class NoWheelSpinBox;
class ProjectBar;
class StarfieldCanvas;

// Shows a solved capture's starfield with the fastest-proper-motion Gaia
// stars in frame circled in yellow, a green line pointing where each is
// heading and a red line pointing where it came from -- see
// ProperMotionOverlay.h for the geometry and StarfieldCanvas.h for the
// rendering. Needs an already-solved image (a .wcs sidecar, same
// convention as the Date tab) plus a Gaia catalog CSV for the same field.
//
// Also usable full-window: the Fullscreen button (or F11) hides the options
// panel and puts the top-level window itself into OS fullscreen, so the
// canvas can take over the whole screen -- the point in the standalone
// EpochFrom-starfield app, and available (via the fullscreenToggled signal)
// for the tabbed EpochFrom-gui to hide its own project bar/tab strip too.
class StarfieldTab : public QWidget {
    Q_OBJECT
public:
    explicit StarfieldTab(ProjectBar *projectBar, QWidget *parent = nullptr);

signals:
    // Emitted whenever this tab's own Fullscreen toggle fires, so an
    // embedding window can hide/show whatever chrome of its own (a tab
    // strip, a project bar) sits outside this widget. This tab always
    // handles the OS-level window fullscreen transition and its own
    // options-panel visibility itself -- a listener only needs to react to
    // its *own* extra UI, if it has any.
    void fullscreenToggled(bool fullscreen);

public slots:
    // Public (not just wired to this tab's own "Fill from Project" button)
    // so the Project bar's consolidated "Fill All Tabs" button can trigger
    // every tab's version of it in one click -- see MainWindow.cpp.
    void fillFromProject();

private slots:
    void browseImage();
    void browseWcs();
    void browseGaia();
    void startLoad();
    void onWorkerFinished(epochfrom::gui::StarfieldWorker::Result result);
    void toggleFullscreen();

private:
    void setBusy(bool busy);

    ProjectBar *projectBar_ = nullptr;

    QLineEdit *imageEdit_ = nullptr;
    QLineEdit *wcsEdit_ = nullptr;
    QLineEdit *gaiaEdit_ = nullptr;
    NoWheelSpinBox *topNSpin_ = nullptr;
    QCheckBox *epochOverrideCheck_ = nullptr;
    NoWheelDoubleSpinBox *epochSpin_ = nullptr;

    QScrollArea *scrollArea_ = nullptr;
    QPushButton *loadButton_ = nullptr;
    QPushButton *fullscreenButton_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    StarfieldCanvas *canvas_ = nullptr;
    bool fullscreenActive_ = false;

    QThread *thread_ = nullptr;
};

} // namespace epochfrom::gui
