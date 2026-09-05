#include "CalibrateTab.h"
#include "CalibrateWorker.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QThread>
#include <QVBoxLayout>

namespace epochfrom::gui {

namespace {
QWidget *rowOf(std::initializer_list<QWidget *> widgets)
{
    auto *w = new QWidget;
    auto *layout = new QHBoxLayout(w);
    layout->setContentsMargins(0, 0, 0, 0);
    for (QWidget *child : widgets)
        layout->addWidget(child);
    return w;
}
} // namespace

CalibrateTab::CalibrateTab(QWidget *parent) : QWidget(parent)
{
    dirEdit_ = new QLineEdit;
    gaiaEdit_ = new QLineEdit;
    outProfileEdit_ = new QLineEdit;
    residualsCsvEdit_ = new QLineEdit;
    auto *browseDirButton = new QPushButton(tr("Browse..."));
    auto *browseGaiaButton = new QPushButton(tr("Browse..."));
    auto *browseOutButton = new QPushButton(tr("Browse..."));
    auto *browseCsvButton = new QPushButton(tr("Browse..."));

    auto *inputGroup = new QGroupBox(tr("Subs, Gaia catalog, and output"));
    auto *inputForm = new QFormLayout(inputGroup);
    inputForm->addRow(tr("Directory of subs:"), rowOf({dirEdit_, browseDirButton}));
    inputForm->addRow(tr("Gaia catalog CSV:"), rowOf({gaiaEdit_, browseGaiaButton}));
    inputForm->addRow(tr("Save profile as:"), rowOf({outProfileEdit_, browseOutButton}));
    inputForm->addRow(tr("Residuals CSV (optional):"), rowOf({residualsCsvEdit_, browseCsvButton}));

    labelEdit_ = new QLineEdit;
    labelEdit_->setPlaceholderText(tr("e.g. 800mm reflector + Paracorr, ASI1600MM"));
    apertureSpin_ = new QDoubleSpinBox;
    apertureSpin_->setRange(0.0, 10000.0);
    apertureSpin_->setSuffix(tr(" mm aperture"));
    focalLengthSpin_ = new QDoubleSpinBox;
    focalLengthSpin_->setRange(0.0, 100000.0);
    focalLengthSpin_->setSuffix(tr(" mm focal length"));
    correctorTypeCombo_ = new QComboBox;
    correctorTypeCombo_->addItems({tr("unknown"), tr("none"), tr("refractive"), tr("reflective"), tr("catadioptric")});
    cameraEdit_ = new QLineEdit;
    pixelSizeSpin_ = new QDoubleSpinBox;
    pixelSizeSpin_->setRange(0.0, 1000.0);
    pixelSizeSpin_->setDecimals(2);
    pixelSizeSpin_->setSuffix(tr(" um pixel size"));
    filterEdit_ = new QLineEdit;
    useValidFromCheck_ = new QCheckBox(tr("Valid from"));
    validFromEdit_ = new QDateEdit(QDate::currentDate());
    validFromEdit_->setCalendarPopup(true);
    validFromEdit_->setEnabled(false);
    useValidToCheck_ = new QCheckBox(tr("Valid to"));
    validToEdit_ = new QDateEdit(QDate::currentDate());
    validToEdit_->setCalendarPopup(true);
    validToEdit_->setEnabled(false);

    auto *metaGroup = new QGroupBox(tr("Equipment profile metadata (descriptive only)"));
    auto *metaForm = new QFormLayout(metaGroup);
    metaForm->addRow(tr("Label:"), labelEdit_);
    metaForm->addRow(tr("Aperture / focal length:"), rowOf({apertureSpin_, focalLengthSpin_}));
    metaForm->addRow(tr("Corrector type:"), correctorTypeCombo_);
    metaForm->addRow(tr("Camera / pixel size:"), rowOf({cameraEdit_, pixelSizeSpin_}));
    metaForm->addRow(tr("Filter:"), filterEdit_);
    metaForm->addRow(useValidFromCheck_, validFromEdit_);
    metaForm->addRow(useValidToCheck_, validToEdit_);

    fwhmSpin_ = new QDoubleSpinBox;
    fwhmSpin_->setRange(0.5, 100.0);
    fwhmSpin_->setValue(4.0);
    fwhmSpin_->setSuffix(tr(" px FWHM"));
    thresholdSpin_ = new QDoubleSpinBox;
    thresholdSpin_->setRange(0.5, 100.0);
    thresholdSpin_->setValue(6.0);
    thresholdSpin_->setSuffix(tr(" sigma threshold"));
    matchArcsecSpin_ = new QDoubleSpinBox;
    matchArcsecSpin_->setRange(0.1, 60.0);
    matchArcsecSpin_->setValue(3.0);
    matchArcsecSpin_->setSuffix(tr(" arcsec match tol."));
    maxOrderSpin_ = new QSpinBox;
    maxOrderSpin_->setRange(1, 10);
    maxOrderSpin_->setValue(6);
    perSubAffineCheck_ = new QCheckBox(tr("Fit and remove each sub's own rotation/scale/shear first (recommended)"));
    perSubAffineCheck_->setChecked(true);
    pixelScaleNormSpin_ = new QDoubleSpinBox;
    pixelScaleNormSpin_->setRange(0.0, 100000.0);
    pixelScaleNormSpin_->setValue(0.0);
    pixelScaleNormSpin_->setSpecialValueText(tr("auto (half the sensor's long axis)"));
    pixelScaleNormSpin_->setSuffix(tr(" px"));

    auto *advancedGroup = new QGroupBox(tr("Detection and fit options"));
    auto *advancedForm = new QFormLayout(advancedGroup);
    advancedForm->addRow(tr("Star detection:"), rowOf({fwhmSpin_, thresholdSpin_}));
    advancedForm->addRow(tr("Gaia cross-match:"), matchArcsecSpin_);
    advancedForm->addRow(tr("Highest polynomial order:"), maxOrderSpin_);
    advancedForm->addRow(tr("Pixel-scale normalization:"), pixelScaleNormSpin_);
    advancedForm->addRow(QString(), perSubAffineCheck_);

    useHintCheck_ = new QCheckBox(tr("Pointing hint"));
    raSpin_ = new QDoubleSpinBox;
    raSpin_->setRange(0.0, 360.0);
    raSpin_->setDecimals(6);
    raSpin_->setSuffix(tr(" deg RA"));
    decSpin_ = new QDoubleSpinBox;
    decSpin_->setRange(-90.0, 90.0);
    decSpin_->setDecimals(6);
    decSpin_->setSuffix(tr(" deg Dec"));
    radiusSpin_ = new QDoubleSpinBox;
    radiusSpin_->setRange(0.01, 180.0);
    radiusSpin_->setValue(1.0);
    radiusSpin_->setSuffix(tr(" deg radius"));
    useScaleCheck_ = new QCheckBox(tr("Pixel-scale bounds"));
    scaleLowSpin_ = new QDoubleSpinBox;
    scaleLowSpin_->setRange(0.01, 1000.0);
    scaleLowSpin_->setDecimals(3);
    scaleLowSpin_->setSuffix(tr(" \"/px low"));
    scaleHighSpin_ = new QDoubleSpinBox;
    scaleHighSpin_->setRange(0.01, 1000.0);
    scaleHighSpin_->setDecimals(3);
    scaleHighSpin_->setValue(10.0);
    scaleHighSpin_->setSuffix(tr(" \"/px high"));
    for (auto *spin : {raSpin_, decSpin_, radiusSpin_, scaleLowSpin_, scaleHighSpin_})
        spin->setEnabled(false);

    auto *hintsGroup = new QGroupBox(tr("Solve hints, for any sub that isn't solved yet"));
    auto *hintsForm = new QFormLayout(hintsGroup);
    hintsForm->addRow(useHintCheck_, rowOf({raSpin_, decSpin_, radiusSpin_}));
    hintsForm->addRow(useScaleCheck_, rowOf({scaleLowSpin_, scaleHighSpin_}));

    calibrateButton_ = new QPushButton(tr("Calibrate"));
    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    logView_->setFont(mono);

    auto *optionsColumn = new QWidget;
    auto *optionsLayout = new QVBoxLayout(optionsColumn);
    optionsLayout->addWidget(inputGroup);
    optionsLayout->addWidget(metaGroup);
    optionsLayout->addWidget(advancedGroup);
    optionsLayout->addWidget(hintsGroup);
    optionsLayout->addWidget(calibrateButton_);
    optionsLayout->addStretch();

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(optionsColumn);
    scrollArea->setWidgetResizable(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(scrollArea, 2);
    mainLayout->addWidget(summaryLabel_);
    mainLayout->addWidget(logView_, 3);

    connect(browseDirButton, &QPushButton::clicked, this, &CalibrateTab::browseDir);
    connect(browseGaiaButton, &QPushButton::clicked, this, &CalibrateTab::browseGaia);
    connect(browseOutButton, &QPushButton::clicked, this, &CalibrateTab::browseOutProfile);
    connect(browseCsvButton, &QPushButton::clicked, this, &CalibrateTab::browseResidualsCsv);
    connect(useValidFromCheck_, &QCheckBox::toggled, validFromEdit_, &QWidget::setEnabled);
    connect(useValidToCheck_, &QCheckBox::toggled, validToEdit_, &QWidget::setEnabled);
    connect(useHintCheck_, &QCheckBox::toggled, this, [this](bool on) {
        raSpin_->setEnabled(on);
        decSpin_->setEnabled(on);
        radiusSpin_->setEnabled(on);
    });
    connect(useScaleCheck_, &QCheckBox::toggled, this, [this](bool on) {
        scaleLowSpin_->setEnabled(on);
        scaleHighSpin_->setEnabled(on);
    });
    connect(calibrateButton_, &QPushButton::clicked, this, &CalibrateTab::startCalibrate);
}

void CalibrateTab::browseDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a directory of subs"));
    if (!dir.isEmpty())
        dirEdit_->setText(dir);
}

void CalibrateTab::browseGaia()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a Gaia catalog CSV"), QString(),
                                                        tr("CSV files (*.csv);;All files (*)"));
    if (!file.isEmpty())
        gaiaEdit_->setText(file);
}

void CalibrateTab::browseOutProfile()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Save equipment profile"), QString(),
                                                        tr("Profile JSON (*.json);;All files (*)"));
    if (!file.isEmpty())
        outProfileEdit_->setText(file);
}

void CalibrateTab::browseResidualsCsv()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Save residuals CSV"), QString(),
                                                        tr("CSV files (*.csv);;All files (*)"));
    if (!file.isEmpty())
        residualsCsvEdit_->setText(file);
}

void CalibrateTab::setBusy(bool busy)
{
    calibrateButton_->setEnabled(!busy);
    calibrateButton_->setText(busy ? tr("Calibrating...") : tr("Calibrate"));
}

void CalibrateTab::startCalibrate()
{
    if (dirEdit_->text().trimmed().isEmpty() || gaiaEdit_->text().trimmed().isEmpty() ||
        outProfileEdit_->text().trimmed().isEmpty()) {
        appendLog(tr("Directory, Gaia catalog, and an output profile path are all required.\n"));
        return;
    }

    CalibrateWorker::Request request;
    request.dirPath = dirEdit_->text().trimmed();
    request.gaiaCsvPath = gaiaEdit_->text().trimmed();
    request.outProfilePath = outProfileEdit_->text().trimmed();
    request.residualsCsvPath = residualsCsvEdit_->text().trimmed();

    request.label = labelEdit_->text();
    request.apertureMm = apertureSpin_->value();
    request.focalLengthMm = focalLengthSpin_->value();
    if (correctorTypeCombo_->currentText() != tr("unknown"))
        request.correctorType = correctorTypeCombo_->currentText();
    request.cameraModel = cameraEdit_->text();
    request.pixelSizeUm = pixelSizeSpin_->value();
    request.filter = filterEdit_->text();
    if (useValidFromCheck_->isChecked())
        request.validFrom = validFromEdit_->date().toString("yyyy-MM-dd");
    if (useValidToCheck_->isChecked())
        request.validTo = validToEdit_->date().toString("yyyy-MM-dd");

    request.calibrationOptions.detection.fwhmPx = fwhmSpin_->value();
    request.calibrationOptions.detection.thresholdSigma = thresholdSpin_->value();
    request.calibrationOptions.matchToleranceArcsec = matchArcsecSpin_->value();
    request.calibrationOptions.pixelScaleNorm = pixelScaleNormSpin_->value();
    QVector<int> orders;
    for (int o = 1; o <= maxOrderSpin_->value(); ++o)
        orders.push_back(o);
    request.calibrationOptions.candidateOrders = orders;
    request.calibrationOptions.fitPerSubAffine = perSubAffineCheck_->isChecked();

    if (useHintCheck_->isChecked()) {
        request.solveOptions.hintRaDeg = raSpin_->value();
        request.solveOptions.hintDecDeg = decSpin_->value();
        request.solveOptions.hintRadiusDeg = radiusSpin_->value();
    }
    if (useScaleCheck_->isChecked()) {
        request.solveOptions.scaleLowArcsecPerPix = scaleLowSpin_->value();
        request.solveOptions.scaleHighArcsecPerPix = scaleHighSpin_->value();
    }

    logView_->clear();
    summaryLabel_->clear();
    setBusy(true);

    auto *worker = new CalibrateWorker(std::move(request));
    thread_ = new QThread(this);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &CalibrateWorker::run);
    connect(worker, &CalibrateWorker::logLine, this, &CalibrateTab::appendLog);
    connect(worker, &CalibrateWorker::summaryReady, this, &CalibrateTab::onSummary);
    connect(worker, &CalibrateWorker::finished, this, &CalibrateTab::onFinished);
    connect(worker, &CalibrateWorker::finished, thread_, &QThread::quit);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

void CalibrateTab::appendLog(const QString &text)
{
    logView_->insertPlainText(text);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void CalibrateTab::onSummary(int chosenOrder, double rmsBeforeMas, double rmsAfterHeldoutMas,
                              double rmsAfterHeldoutStdMas, int nSubsUsed, int nSubsTotal)
{
    summaryLabel_->setText(tr("Subs used: %1/%2  |  order %3  |  RMS %4 mas -> %5 +/- %6 mas (held-out)")
                                .arg(nSubsUsed)
                                .arg(nSubsTotal)
                                .arg(chosenOrder)
                                .arg(QString::number(rmsBeforeMas, 'f', 1))
                                .arg(QString::number(rmsAfterHeldoutMas, 'f', 1))
                                .arg(QString::number(rmsAfterHeldoutStdMas, 'f', 1)));
}

void CalibrateTab::onFinished(bool ok)
{
    Q_UNUSED(ok);
    setBusy(false);
    thread_ = nullptr;
}

} // namespace epochfrom::gui
