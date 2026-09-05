#pragma once

#include <QProcess>
#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace epochfrom::gui {

class ProjectBar;

// The "Gaia" tab: wraps scripts/gaia_field_query.py, which downloads a
// Gaia DR3 reference catalog for a field -- the catalog every Calibrate/
// Date fit is matched against. Unlike Solve/Calibrate/Date, this isn't
// part of epoch_from_core: it's a separate Python script (needs
// astropy/astroquery, and talks to ESA's TAP archive over the network), so
// it's run as a subprocess via QProcess -- which is already asynchronous
// on its own, so no worker/QThread pair is needed here.
class GaiaTab : public QWidget {
    Q_OBJECT
public:
    // `projectBar` may be null (the tab still works standalone); when set,
    // its base directory backs this tab's "Fill from Project" button.
    explicit GaiaTab(ProjectBar *projectBar, QWidget *parent = nullptr);
    ~GaiaTab() override;

private slots:
    void browseFits();
    void browseOut();
    void fillFromProject();
    void startQuery();
    void cancelQuery();
    void onReadyRead();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessErrorOccurred(QProcess::ProcessError error);

private:
    void setBusy(bool busy);
    void appendLog(const QString &text);
    QString findScript() const;

    ProjectBar *projectBar_;

    QRadioButton *fitsRadio_;
    QRadioButton *raDecRadio_;
    QRadioButton *presetRadio_;

    QLineEdit *fitsPathEdit_;

    QDoubleSpinBox *raSpin_;
    QDoubleSpinBox *decSpin_;

    QComboBox *presetCombo_;

    QDoubleSpinBox *radiusSpin_;
    QDoubleSpinBox *maglimSpin_;
    QDoubleSpinBox *maxRuweSpin_;
    QSpinBox *limitSpin_;

    QLineEdit *outPathEdit_;
    QLineEdit *pythonPathEdit_;
    QLineEdit *scriptPathEdit_;

    QPushButton *queryButton_;
    QPushButton *cancelButton_;
    QPlainTextEdit *logView_;

    QProcess *process_ = nullptr;
};

} // namespace epochfrom::gui
