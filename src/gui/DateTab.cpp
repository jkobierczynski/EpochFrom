#include "DateTab.h"
#include "DateWorker.h"

#include <QCheckBox>
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
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QVBoxLayout>
#include <limits>

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

DateTab::DateTab(QWidget *parent) : QWidget(parent)
{
    singleFileRadio_ = new QRadioButton(tr("Single image"));
    directoryRadio_ = new QRadioButton(tr("Directory (batch)"));
    singleFileRadio_->setChecked(true);
    pathEdit_ = new QLineEdit;
    auto *browsePathButton = new QPushButton(tr("Browse..."));
    wcsEdit_ = new QLineEdit;
    wcsEdit_->setPlaceholderText(tr("default: <image>.wcs next to it (single image only)"));
    auto *browseWcsButton = new QPushButton(tr("Browse..."));
    gaiaEdit_ = new QLineEdit;
    auto *browseGaiaButton = new QPushButton(tr("Browse..."));
    profileEdit_ = new QLineEdit;
    auto *browseProfileButton = new QPushButton(tr("Browse..."));
    noProfileCheck_ = new QCheckBox(
        tr("Date without an equipment profile (not recommended -- estimated dates can be off by "
           "years on a rig with any real uncorrected optical distortion)"));

    auto *inputGroup = new QGroupBox(tr("What to date"));
    auto *inputLayout = new QVBoxLayout(inputGroup);
    auto *radioRow = new QHBoxLayout;
    radioRow->addWidget(singleFileRadio_);
    radioRow->addWidget(directoryRadio_);
    radioRow->addStretch();
    inputLayout->addLayout(radioRow);
    auto *form = new QFormLayout;
    form->addRow(tr("Image / directory:"), rowOf({pathEdit_, browsePathButton}));
    form->addRow(tr("WCS override:"), rowOf({wcsEdit_, browseWcsButton}));
    form->addRow(tr("Gaia catalog CSV:"), rowOf({gaiaEdit_, browseGaiaButton}));
    form->addRow(tr("Equipment profile:"), rowOf({profileEdit_, browseProfileButton}));
    inputLayout->addLayout(form);
    inputLayout->addWidget(noProfileCheck_);

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
    useObsSigmaCheck_ = new QCheckBox(tr("Override assumed per-star precision"));
    obsSigmaSpin_ = new QDoubleSpinBox;
    obsSigmaSpin_->setRange(1.0, 10000.0);
    obsSigmaSpin_->setValue(300.0);
    obsSigmaSpin_->setSuffix(tr(" mas"));
    obsSigmaSpin_->setEnabled(false);

    auto *detectionGroup = new QGroupBox(tr("Star detection and fit options"));
    auto *detectionForm = new QFormLayout(detectionGroup);
    detectionForm->addRow(tr("Star detection:"), rowOf({fwhmSpin_, thresholdSpin_}));
    detectionForm->addRow(tr("Gaia cross-match:"), matchArcsecSpin_);
    detectionForm->addRow(useObsSigmaCheck_, obsSigmaSpin_);

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

    auto *hintsGroup = new QGroupBox(tr("Solve hints, for any image that isn't solved yet"));
    auto *hintsForm = new QFormLayout(hintsGroup);
    hintsForm->addRow(useHintCheck_, rowOf({raSpin_, decSpin_, radiusSpin_}));
    hintsForm->addRow(useScaleCheck_, rowOf({scaleLowSpin_, scaleHighSpin_}));

    dateButton_ = new QPushButton(tr("Date"));
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
    optionsLayout->addWidget(detectionGroup);
    optionsLayout->addWidget(hintsGroup);
    optionsLayout->addWidget(dateButton_);
    optionsLayout->addStretch();

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(optionsColumn);
    scrollArea->setWidgetResizable(true);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(scrollArea, 2);
    mainLayout->addWidget(summaryLabel_);
    mainLayout->addWidget(logView_, 3);

    connect(directoryRadio_, &QRadioButton::toggled, this, [this](bool isDir) {
        wcsEdit_->setEnabled(!isDir);
    });
    connect(browsePathButton, &QPushButton::clicked, this, &DateTab::browsePath);
    connect(browseWcsButton, &QPushButton::clicked, this, &DateTab::browseWcs);
    connect(browseGaiaButton, &QPushButton::clicked, this, &DateTab::browseGaia);
    connect(browseProfileButton, &QPushButton::clicked, this, &DateTab::browseProfile);
    connect(noProfileCheck_, &QCheckBox::toggled, this, [this](bool on) {
        profileEdit_->setEnabled(!on);
    });
    connect(useObsSigmaCheck_, &QCheckBox::toggled, obsSigmaSpin_, &QWidget::setEnabled);
    connect(useHintCheck_, &QCheckBox::toggled, this, [this](bool on) {
        raSpin_->setEnabled(on);
        decSpin_->setEnabled(on);
        radiusSpin_->setEnabled(on);
    });
    connect(useScaleCheck_, &QCheckBox::toggled, this, [this](bool on) {
        scaleLowSpin_->setEnabled(on);
        scaleHighSpin_->setEnabled(on);
    });
    connect(dateButton_, &QPushButton::clicked, this, &DateTab::startDate);
}

void DateTab::browsePath()
{
    if (directoryRadio_->isChecked()) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a directory of images"));
        if (!dir.isEmpty())
            pathEdit_->setText(dir);
    } else {
        const QString file = QFileDialog::getOpenFileName(this, tr("Choose an image"), QString(),
                                                            tr("FITS images (*.fits *.fit *.fts);;All files (*)"));
        if (!file.isEmpty())
            pathEdit_->setText(file);
    }
}

void DateTab::browseWcs()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a .wcs file"), QString(),
                                                        tr("WCS files (*.wcs);;All files (*)"));
    if (!file.isEmpty())
        wcsEdit_->setText(file);
}

void DateTab::browseGaia()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a Gaia catalog CSV"), QString(),
                                                        tr("CSV files (*.csv);;All files (*)"));
    if (!file.isEmpty())
        gaiaEdit_->setText(file);
}

void DateTab::browseProfile()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose an equipment profile"), QString(),
                                                        tr("Profile JSON (*.json);;All files (*)"));
    if (!file.isEmpty())
        profileEdit_->setText(file);
}

void DateTab::setBusy(bool busy)
{
    dateButton_->setEnabled(!busy);
    dateButton_->setText(busy ? tr("Dating...") : tr("Date"));
}

void DateTab::startDate()
{
    if (pathEdit_->text().trimmed().isEmpty() || gaiaEdit_->text().trimmed().isEmpty()) {
        appendLog(tr("An image (or directory) and a Gaia catalog are both required.\n"));
        return;
    }
    if (!noProfileCheck_->isChecked() && profileEdit_->text().trimmed().isEmpty()) {
        appendLog(tr("An equipment profile is required unless \"date without a profile\" is "
                      "checked -- an uncorrected rig's own optical distortion can swamp the "
                      "proper-motion signal this fit depends on.\n"));
        return;
    }

    DateWorker::Request request;
    request.isDir = directoryRadio_->isChecked();
    request.path = pathEdit_->text().trimmed();
    if (!request.isDir)
        request.wcsPath = wcsEdit_->text().trimmed();
    request.gaiaCsvPath = gaiaEdit_->text().trimmed();
    request.profilePath = noProfileCheck_->isChecked() ? QString() : profileEdit_->text().trimmed();

    request.dateOptions.detection.fwhmPx = fwhmSpin_->value();
    request.dateOptions.detection.thresholdSigma = thresholdSpin_->value();
    request.dateOptions.maxMatchArcsec = matchArcsecSpin_->value();
    if (useObsSigmaCheck_->isChecked())
        request.dateOptions.obsSigmaMas = obsSigmaSpin_->value();

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

    auto *worker = new DateWorker(std::move(request));
    thread_ = new QThread(this);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &DateWorker::run);
    connect(worker, &DateWorker::logLine, this, &DateTab::appendLog);
    connect(worker, &DateWorker::summaryReady, this, &DateTab::onSummary);
    connect(worker, &DateWorker::finished, this, &DateTab::onFinished);
    connect(worker, &DateWorker::finished, thread_, &QThread::quit);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

void DateTab::appendLog(const QString &text)
{
    logView_->insertPlainText(text);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void DateTab::onSummary(const QString &estimatedDate, double epochJyear, double epochSigmaYears,
                         double rmsResidualMas)
{
    summaryLabel_->setText(tr("Estimated date: %1  (epoch %2 +/- %3 yr, RMS %4 mas)")
                                .arg(estimatedDate)
                                .arg(QString::number(epochJyear, 'f', 4))
                                .arg(QString::number(epochSigmaYears, 'f', 4))
                                .arg(QString::number(rmsResidualMas, 'f', 1)));
}

void DateTab::onFinished(bool ok)
{
    Q_UNUSED(ok);
    setBusy(false);
    thread_ = nullptr;
}

} // namespace epochfrom::gui
