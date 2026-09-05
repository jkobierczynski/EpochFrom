#pragma once

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QThread;

namespace epochfrom::gui {

// The "Solve" tab: wraps PlateSolver over a single image or a whole
// directory (mirrors `EpochFrom solve` / `EpochFrom solve --dir`).
class SolveTab : public QWidget {
    Q_OBJECT
public:
    explicit SolveTab(QWidget *parent = nullptr);

private slots:
    void browsePath();
    void startSolve();
    void appendLog(const QString &text);
    void onFinished(bool ok);

private:
    void setBusy(bool busy);

    QRadioButton *singleFileRadio_;
    QRadioButton *directoryRadio_;
    QLineEdit *pathEdit_;
    QPushButton *browseButton_;
    QCheckBox *wcsOnlyCheck_;
    QCheckBox *forceCheck_;

    QCheckBox *useHintCheck_;
    QDoubleSpinBox *raSpin_;
    QDoubleSpinBox *decSpin_;
    QDoubleSpinBox *radiusSpin_;
    QCheckBox *useScaleCheck_;
    QDoubleSpinBox *scaleLowSpin_;
    QDoubleSpinBox *scaleHighSpin_;
    QSpinBox *downsampleSpin_;
    QSpinBox *cpuLimitSpin_;
    QLineEdit *solveFieldPathEdit_;

    QPushButton *solveButton_;
    QPlainTextEdit *logView_;

    QThread *thread_ = nullptr;
};

} // namespace epochfrom::gui
