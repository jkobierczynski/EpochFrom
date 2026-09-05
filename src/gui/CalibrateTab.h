#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QThread;

namespace epochfrom::gui {

// The "Calibrate" tab: wraps EquipmentCalibrator over a directory of subs
// (mirrors `EpochFrom calibrate --dir`). Only the --dir input mode is
// exposed -- see CalibrateWorker's class comment for why.
class CalibrateTab : public QWidget {
    Q_OBJECT
public:
    explicit CalibrateTab(QWidget *parent = nullptr);

private slots:
    void browseDir();
    void browseGaia();
    void browseOutProfile();
    void browseResidualsCsv();
    void startCalibrate();
    void appendLog(const QString &text);
    void onSummary(int chosenOrder, double rmsBeforeMas, double rmsAfterHeldoutMas,
                    double rmsAfterHeldoutStdMas, int nSubsUsed, int nSubsTotal);
    void onFinished(bool ok);

private:
    void setBusy(bool busy);

    QLineEdit *dirEdit_;
    QLineEdit *gaiaEdit_;
    QLineEdit *outProfileEdit_;
    QLineEdit *residualsCsvEdit_;

    QLineEdit *labelEdit_;
    QDoubleSpinBox *apertureSpin_;
    QDoubleSpinBox *focalLengthSpin_;
    QComboBox *correctorTypeCombo_;
    QLineEdit *cameraEdit_;
    QDoubleSpinBox *pixelSizeSpin_;
    QLineEdit *filterEdit_;
    QCheckBox *useValidFromCheck_;
    QDateEdit *validFromEdit_;
    QCheckBox *useValidToCheck_;
    QDateEdit *validToEdit_;

    QDoubleSpinBox *fwhmSpin_;
    QDoubleSpinBox *thresholdSpin_;
    QDoubleSpinBox *matchArcsecSpin_;
    QSpinBox *maxOrderSpin_;
    QCheckBox *perSubAffineCheck_;
    QDoubleSpinBox *pixelScaleNormSpin_;

    QCheckBox *useHintCheck_;
    QDoubleSpinBox *raSpin_;
    QDoubleSpinBox *decSpin_;
    QDoubleSpinBox *radiusSpin_;
    QCheckBox *useScaleCheck_;
    QDoubleSpinBox *scaleLowSpin_;
    QDoubleSpinBox *scaleHighSpin_;

    QPushButton *calibrateButton_;
    QPlainTextEdit *logView_;
    QLabel *summaryLabel_;

    QThread *thread_ = nullptr;
};

} // namespace epochfrom::gui
