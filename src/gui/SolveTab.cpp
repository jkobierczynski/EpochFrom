#include "SolveTab.h"
#include "NoWheelWidgets.h"
#include "ProjectBar.h"
#include "SolveWorker.h"
#include "TabLayoutHelpers.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QSplitter>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace epochfrom::gui {

SolveTab::SolveTab(ProjectBar *projectBar, QWidget *parent) : QWidget(parent), projectBar_(projectBar)
{
    singleFileRadio_ = new QRadioButton(tr("Single image"));
    directoryRadio_ = new QRadioButton(tr("Directory (batch)"));
    singleFileRadio_->setChecked(true);

    pathEdit_ = new QLineEdit;
    browseButton_ = new QPushButton(tr("Browse..."));
    wcsOnlyCheck_ = new QCheckBox(tr("Skip solving -- read an existing .wcs file instead"));
    forceCheck_ = new QCheckBox(tr("Re-solve files that already have a .wcs sidecar"));
    forceCheck_->setEnabled(false);
    updateFitsHeaderCheck_ = new QCheckBox(
        tr("Also write the WCS into the image's own FITS header (modifies the original file!)"));
    updateFitsHeaderCheck_->setToolTip(
        tr("Off by default. EpochFrom always writes the solved WCS to a separate .wcs sidecar "
           "file next to the image -- turning this on additionally copies CRVAL/CRPIX/CD, any "
           "SIP distortion terms, and convenience RA/DEC keys directly into the image's own "
           "FITS header, in place. This edits your original light frame on disk; it does not "
           "touch OBJCTRA/OBJCTDEC (the mount's original requested pointing)."));

    auto *fromProjectButton = new QPushButton(tr("Fill from Project"));
    fromProjectButton->setToolTip(
        tr("Use the project bar's base directory + filter as the directory to solve"));

    auto *inputGroup = new QGroupBox(tr("What to solve"));
    auto *inputLayout = new QVBoxLayout;
    auto *radioRow = new QHBoxLayout;
    radioRow->addWidget(singleFileRadio_);
    radioRow->addWidget(directoryRadio_);
    radioRow->addStretch();
    radioRow->addWidget(fromProjectButton);
    inputLayout->addLayout(radioRow);
    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(pathEdit_);
    pathRow->addWidget(browseButton_);
    inputLayout->addLayout(pathRow);
    inputLayout->addWidget(wcsOnlyCheck_);
    inputLayout->addWidget(forceCheck_);
    inputLayout->addWidget(updateFitsHeaderCheck_);
    inputGroup->setLayout(inputLayout);

    useHintCheck_ = new QCheckBox(tr("Pointing hint (much faster than a blind solve)"));
    raSpin_ = new NoWheelDoubleSpinBox;
    raSpin_->setRange(0.0, 360.0);
    raSpin_->setDecimals(6);
    raSpin_->setSuffix(tr(" deg RA"));
    decSpin_ = new NoWheelDoubleSpinBox;
    decSpin_->setRange(-90.0, 90.0);
    decSpin_->setDecimals(6);
    decSpin_->setSuffix(tr(" deg Dec"));
    radiusSpin_ = new NoWheelDoubleSpinBox;
    radiusSpin_->setRange(0.01, 180.0);
    radiusSpin_->setValue(1.0);
    radiusSpin_->setSuffix(tr(" deg radius"));

    useScaleCheck_ = new QCheckBox(tr("Pixel-scale bounds"));
    scaleLowSpin_ = new NoWheelDoubleSpinBox;
    scaleLowSpin_->setRange(0.01, 1000.0);
    scaleLowSpin_->setDecimals(3);
    scaleLowSpin_->setSuffix(tr(" \"/px low"));
    scaleHighSpin_ = new NoWheelDoubleSpinBox;
    scaleHighSpin_->setRange(0.01, 1000.0);
    scaleHighSpin_->setDecimals(3);
    scaleHighSpin_->setValue(10.0);
    scaleHighSpin_->setSuffix(tr(" \"/px high"));

    downsampleSpin_ = new NoWheelSpinBox;
    downsampleSpin_->setRange(1, 8);
    downsampleSpin_->setValue(2);
    cpuLimitSpin_ = new NoWheelSpinBox;
    cpuLimitSpin_->setRange(5, 3600);
    cpuLimitSpin_->setValue(55);
    cpuLimitSpin_->setSuffix(tr(" sec"));
    solveFieldPathEdit_ = new QLineEdit(QStringLiteral("solve-field"));
    // "solve-field" alone is correct on Linux (astrometry.net's own build,
    // expected on PATH) but never right on Windows -- astrometry.net has
    // no native Windows build at all, so real Windows use goes through
    // ansvr's Cygwin-hosted solve-field via scripts/ansvr-solve-field.bat
    // (see docs/windows-port.md's ansvr/Cygwin FAQ for why that wrapper
    // exists). If it's sitting next to this binary -- which it is, in a
    // normal build, since src/gui/CMakeLists.txt copies scripts/ there --
    // default straight to it instead of leaving everyone to browse to it
    // or type the path in by hand on every fresh install.
#if defined(Q_OS_WIN)
    const QString detectedAnsvrWrapper = findAnsvrWrapper();
    if (!detectedAnsvrWrapper.isEmpty())
        solveFieldPathEdit_->setText(detectedAnsvrWrapper);
#endif

    for (auto *spin : {raSpin_, decSpin_, radiusSpin_})
        spin->setEnabled(false);
    for (auto *spin : {scaleLowSpin_, scaleHighSpin_})
        spin->setEnabled(false);

    auto *hintsGroup = new QGroupBox(tr("Solve hints (optional, but recommended)"));
    auto *hintsLayout = new QFormLayout;
    auto *hintRowWidget = new QWidget;
    auto *hintRow = new QHBoxLayout(hintRowWidget);
    hintRow->setContentsMargins(0, 0, 0, 0);
    hintRow->addWidget(raSpin_);
    hintRow->addWidget(decSpin_);
    hintRow->addWidget(radiusSpin_);
    hintsLayout->addRow(useHintCheck_, hintRowWidget);
    auto *scaleRowWidget = new QWidget;
    auto *scaleRow = new QHBoxLayout(scaleRowWidget);
    scaleRow->setContentsMargins(0, 0, 0, 0);
    scaleRow->addWidget(scaleLowSpin_);
    scaleRow->addWidget(scaleHighSpin_);
    hintsLayout->addRow(useScaleCheck_, scaleRowWidget);
    hintsLayout->addRow(tr("Downsample:"), downsampleSpin_);
    hintsLayout->addRow(tr("CPU limit:"), cpuLimitSpin_);
    hintsLayout->addRow(tr("solve-field path:"), solveFieldPathEdit_);
    hintsGroup->setLayout(hintsLayout);

    solveButton_ = new QPushButton(tr("Solve"));
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    logView_->setFont(mono);

    auto *optionsColumn = new QWidget;
    auto *optionsLayout = new QVBoxLayout(optionsColumn);
    optionsLayout->addWidget(inputGroup);
    optionsLayout->addWidget(hintsGroup);
    optionsLayout->addStretch();

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(optionsColumn);
    scrollArea->setWidgetResizable(true);

    // The options pane and the log share a QSplitter so the divider between
    // them can be dragged, or snapped with a preset -- scrolling the mouse
    // wheel over the options no longer fights for that space (see
    // NoWheelWidgets.h), and it no longer changes spin/combo box values
    // either.
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(scrollArea);
    splitter->addWidget(logView_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    applyInitialSplitterRatio(splitter, 1, 1);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(makeActionBar(splitter, solveButton_));

    connect(directoryRadio_, &QRadioButton::toggled, this, [this](bool isDir) {
        wcsOnlyCheck_->setEnabled(!isDir);
        forceCheck_->setEnabled(isDir);
    });
    connect(wcsOnlyCheck_, &QCheckBox::toggled, this, [this](bool wcsOnly) {
        // --wcs-only just re-parses an existing .wcs file; there's no actual
        // solve-field run and no image to write a header into, so this
        // option would be silently ignored -- disable it rather than leave
        // a checkbox that looks live but does nothing.
        updateFitsHeaderCheck_->setEnabled(!wcsOnly);
    });
    connect(useHintCheck_, &QCheckBox::toggled, this, [this](bool on) {
        raSpin_->setEnabled(on);
        decSpin_->setEnabled(on);
        radiusSpin_->setEnabled(on);
    });
    connect(useScaleCheck_, &QCheckBox::toggled, this, [this](bool on) {
        scaleLowSpin_->setEnabled(on);
        scaleHighSpin_->setEnabled(on);
    });
    connect(browseButton_, &QPushButton::clicked, this, &SolveTab::browsePath);
    connect(fromProjectButton, &QPushButton::clicked, this, &SolveTab::fillFromProject);
    connect(solveButton_, &QPushButton::clicked, this, &SolveTab::startSolve);
}

void SolveTab::fillFromProject()
{
    if (!projectBar_ || projectBar_->baseDir().isEmpty()) {
        appendLog(tr("Set a base directory in the Project bar above first.\n"));
        return;
    }
    directoryRadio_->setChecked(true);
    pathEdit_->setText(projectBar_->subsDir());
}

void SolveTab::browsePath()
{
    if (directoryRadio_->isChecked()) {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a directory of subs"));
        if (!dir.isEmpty())
            pathEdit_->setText(dir);
    } else {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Choose an image"), QString(),
            tr("FITS images (*.fits *.fit *.fts);;WCS files (*.wcs);;All files (*)"));
        if (!file.isEmpty())
            pathEdit_->setText(file);
    }
}

QString SolveTab::findAnsvrWrapper() const
{
    // Same approach as GaiaTab::findScript() -- look in the handful of
    // places scripts/ansvr-solve-field.bat plausibly sits relative to
    // wherever this binary is actually running from. The first candidate
    // ("right beside me, in a scripts/ subfolder") is the expected hit in
    // a normal build, since src/gui/CMakeLists.txt copies the whole
    // scripts/ directory there; the rest are a fallback for anything laid
    // out unexpectedly.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("scripts/ansvr-solve-field.bat"),
        QDir(appDir).filePath("../scripts/ansvr-solve-field.bat"),
        QDir(appDir).filePath("../../scripts/ansvr-solve-field.bat"),
        QDir(appDir).filePath("../../../scripts/ansvr-solve-field.bat"),
        QDir::current().filePath("scripts/ansvr-solve-field.bat"),
    };
    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (QFileInfo::exists(cleaned))
            return cleaned;
    }
    return QString();
}

void SolveTab::setBusy(bool busy)
{
    solveButton_->setEnabled(!busy);
    solveButton_->setText(busy ? tr("Solving...") : tr("Solve"));
}

void SolveTab::startSolve()
{
    if (pathEdit_->text().trimmed().isEmpty()) {
        appendLog(tr("Pick an image or directory first.\n"));
        return;
    }

    SolveWorker::Request request;
    request.isDir = directoryRadio_->isChecked();
    request.path = pathEdit_->text().trimmed();
    request.wcsOnly = wcsOnlyCheck_->isChecked();
    request.force = forceCheck_->isChecked();

    if (useHintCheck_->isChecked()) {
        request.options.hintRaDeg = raSpin_->value();
        request.options.hintDecDeg = decSpin_->value();
        request.options.hintRadiusDeg = radiusSpin_->value();
    }
    if (useScaleCheck_->isChecked()) {
        request.options.scaleLowArcsecPerPix = scaleLowSpin_->value();
        request.options.scaleHighArcsecPerPix = scaleHighSpin_->value();
    }
    request.options.downsample = downsampleSpin_->value();
    request.options.cpuLimitSeconds = cpuLimitSpin_->value();
    if (!solveFieldPathEdit_->text().trimmed().isEmpty())
        request.options.solveFieldPath = solveFieldPathEdit_->text().trimmed();
    request.options.updateFitsHeader = updateFitsHeaderCheck_->isChecked();

    logView_->clear();
    setBusy(true);

    auto *worker = new SolveWorker(std::move(request));
    thread_ = new QThread(this);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &SolveWorker::run);
    connect(worker, &SolveWorker::logLine, this, &SolveTab::appendLog);
    connect(worker, &SolveWorker::singleSolveReady, this, &SolveTab::onSingleSolveReady);
    connect(worker, &SolveWorker::finished, this, &SolveTab::onFinished);
    connect(worker, &SolveWorker::finished, thread_, &QThread::quit);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

void SolveTab::appendLog(const QString &text)
{
    logView_->insertPlainText(text);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

void SolveTab::onSingleSolveReady(epochfrom::PlateSolveResult result)
{
    // Fires only from the single-image branch (see SolveWorker::run()), for
    // both a fresh solve-field run and a --wcs-only read. The idea: once one
    // sub from a session is solved, its result is a near-exact pointing hint
    // for the rest of that same session's subs -- the mount barely moves
    // (dithering aside) and the rig's pixel scale obviously doesn't change --
    // so prefill both hint groups and point the directory field at this
    // image's own folder, leaving just one more click (Solve, now in
    // directory/batch mode) to process the rest.
    if (!result.solved)
        return;

    raSpin_->setValue(result.centerRaDeg);
    decSpin_->setValue(result.centerDecDeg);
    // A generous but still narrow radius: twice the solved field's own
    // diagonal, clamped to a sane range, to comfortably cover ordinary
    // dithering between subs while still being far tighter (and faster)
    // than a blind solve.
    const double fieldDiagonalDeg =
        std::hypot(result.fieldWidthArcmin, result.fieldHeightArcmin) / 60.0;
    double radiusDeg = fieldDiagonalDeg * 2.0;
    if (!std::isfinite(radiusDeg) || radiusDeg <= 0.0)
        radiusDeg = 1.0;
    radiusDeg = std::clamp(radiusDeg, 1.0, 5.0);
    radiusSpin_->setValue(radiusDeg);
    useHintCheck_->setChecked(true);

    if (result.pixelScaleArcsecPerPix > 0.0) {
        scaleLowSpin_->setValue(result.pixelScaleArcsecPerPix * 0.8);
        scaleHighSpin_->setValue(result.pixelScaleArcsecPerPix * 1.2);
        useScaleCheck_->setChecked(true);
    }

    // Switch to directory mode, pointed at the just-solved image's own
    // folder, so the only remaining step is another click of Solve.
    const QString solvedDir = QFileInfo(pathEdit_->text().trimmed()).dir().path();
    directoryRadio_->setChecked(true);
    if (!solvedDir.isEmpty())
        pathEdit_->setText(solvedDir);

    appendLog(tr("Prefilled pointing hint (RA %1 deg, Dec %2 deg, radius %3 deg) and pixel-scale "
                 "bounds (%4-%5 \"/px) from this solve -- switched to directory mode. Click Solve "
                 "again to batch-solve the rest of %6.\n")
                  .arg(result.centerRaDeg, 0, 'f', 4)
                  .arg(result.centerDecDeg, 0, 'f', 4)
                  .arg(radiusDeg, 0, 'f', 2)
                  .arg(scaleLowSpin_->value(), 0, 'f', 3)
                  .arg(scaleHighSpin_->value(), 0, 'f', 3)
                  .arg(solvedDir));
}

void SolveTab::onFinished(bool ok)
{
    Q_UNUSED(ok);
    setBusy(false);
    thread_ = nullptr;
}

} // namespace epochfrom::gui
