#pragma once

#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QThread;

namespace epochfrom::gui {

// The "Date" tab: wraps ImageDater over a single image or a directory
// (mirrors `EpochFrom date` / `EpochFrom date --dir`).
class DateTab : public QWidget {
    Q_OBJECT
public:
    explicit DateTab(QWidget *parent = nullptr);

private slots:
    void browsePath();
    void browseWcs();
    void browseGaia();
    void browseProfile();
    void startDate();
    void appendLog(const QString &text);
    void onSummary(const QString &estimatedDate, double epochJyear, double epochSigmaYears,
                    double rmsResidualMas);
    void onFinished(bool ok);

private:
    void setBusy(bool busy);

    QRadioButton *singleFileRadio_;
    QRadioButton *directoryRadio_;
    QLineEdit *pathEdit_;
    QLineEdit *wcsEdit_;
    QLineEdit *gaiaEdit_;
    QLineEdit *profileEdit_;
    QCheckBox *noProfileCheck_;

    QDoubleSpinBox *fwhmSpin_;
    QDoubleSpinBox *thresholdSpin_;
    QDoubleSpinBox *matchArcsecSpin_;
    QCheckBox *useObsSigmaCheck_;
    QDoubleSpinBox *obsSigmaSpin_;

    QCheckBox *useHintCheck_;
    QDoubleSpinBox *raSpin_;
    QDoubleSpinBox *decSpin_;
    QDoubleSpinBox *radiusSpin_;
    QCheckBox *useScaleCheck_;
    QDoubleSpinBox *scaleLowSpin_;
    QDoubleSpinBox *scaleHighSpin_;

    QPushButton *dateButton_;
    QPlainTextEdit *logView_;
    QLabel *summaryLabel_;

    QThread *thread_ = nullptr;
};

} // namespace epochfrom::gui
