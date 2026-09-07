#pragma once

#include "StarfieldWorker.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
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
class StarfieldTab : public QWidget {
    Q_OBJECT
public:
    explicit StarfieldTab(ProjectBar *projectBar, QWidget *parent = nullptr);

private slots:
    void browseImage();
    void browseWcs();
    void browseGaia();
    void fillFromProject();
    void startLoad();
    void onWorkerFinished(epochfrom::gui::StarfieldWorker::Result result);

private:
    void setBusy(bool busy);

    ProjectBar *projectBar_ = nullptr;

    QLineEdit *imageEdit_ = nullptr;
    QLineEdit *wcsEdit_ = nullptr;
    QLineEdit *gaiaEdit_ = nullptr;
    NoWheelSpinBox *topNSpin_ = nullptr;
    QCheckBox *epochOverrideCheck_ = nullptr;
    NoWheelDoubleSpinBox *epochSpin_ = nullptr;

    QPushButton *loadButton_ = nullptr;
    QLabel *summaryLabel_ = nullptr;
    StarfieldCanvas *canvas_ = nullptr;

    QThread *thread_ = nullptr;
};

} // namespace epochfrom::gui
