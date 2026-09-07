#include "StarfieldTab.h"
#include "NoWheelWidgets.h"
#include "ProjectBar.h"
#include "ReportFormatting.h"
#include "StarfieldCanvas.h"
#include "TabLayoutHelpers.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
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

StarfieldTab::StarfieldTab(ProjectBar *projectBar, QWidget *parent) : QWidget(parent), projectBar_(projectBar)
{
    imageEdit_ = new QLineEdit;
    imageEdit_->setPlaceholderText(tr("a solved FITS image"));
    auto *browseImageButton = new QPushButton(tr("Browse..."));
    wcsEdit_ = new QLineEdit;
    wcsEdit_->setPlaceholderText(tr("default: <image>.wcs next to it"));
    auto *browseWcsButton = new QPushButton(tr("Browse..."));
    gaiaEdit_ = new QLineEdit;
    auto *browseGaiaButton = new QPushButton(tr("Browse..."));
    auto *fromProjectButton = new QPushButton(tr("Fill from Project"));
    fromProjectButton->setToolTip(tr("Use the project bar's Gaia catalog CSV below"));

    topNSpin_ = new NoWheelSpinBox;
    topNSpin_->setRange(1, 500);
    topNSpin_->setValue(15);
    topNSpin_->setSuffix(tr(" stars"));
    topNSpin_->setToolTip(tr("How many of the fastest-proper-motion Gaia stars actually inside "
                              "this frame to circle"));

    epochOverrideCheck_ = new QCheckBox(tr("Override display epoch"));
    epochSpin_ = new NoWheelDoubleSpinBox;
    epochSpin_->setRange(1800.0, 2300.0);
    epochSpin_->setDecimals(3);
    epochSpin_->setValue(2000.0);
    epochSpin_->setSuffix(tr(" (Julian year)"));
    epochSpin_->setEnabled(false);
    epochSpin_->setToolTip(tr("Where to place each star's yellow circle. Defaults to the image's "
                               "own DATE-OBS -- override to preview where these stars will be at "
                               "some other epoch, e.g. decades from now."));

    auto *inputGroup = new QGroupBox(tr("What to show"));
    auto *inputLayout = new QVBoxLayout(inputGroup);
    auto *form = new QFormLayout;
    form->addRow(tr("Image:"), rowOf({imageEdit_, browseImageButton}));
    form->addRow(tr("WCS override:"), rowOf({wcsEdit_, browseWcsButton}));
    form->addRow(tr("Gaia catalog CSV:"), rowOf({gaiaEdit_, browseGaiaButton, fromProjectButton}));
    form->addRow(tr("Top N by proper motion:"), topNSpin_);
    form->addRow(epochOverrideCheck_, epochSpin_);
    inputLayout->addLayout(form);

    auto *optionsColumn = new QWidget;
    auto *optionsLayout = new QVBoxLayout(optionsColumn);
    optionsLayout->addWidget(inputGroup);
    optionsLayout->addStretch();

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(optionsColumn);
    scrollArea->setWidgetResizable(true);

    loadButton_ = new QPushButton(tr("Load"));
    summaryLabel_ = new QLabel;
    summaryLabel_->setWordWrap(true);
    canvas_ = new StarfieldCanvas;

    auto *outputColumn = new QWidget;
    auto *outputLayout = new QVBoxLayout(outputColumn);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->addWidget(summaryLabel_);
    outputLayout->addWidget(canvas_, 1);

    // See SolveTab.cpp for why the options and the output share a
    // QSplitter instead of a flat, all-scrolling column.
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(scrollArea);
    splitter->addWidget(outputColumn);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({1, 3});

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(makeActionBar(splitter, loadButton_));

    connect(browseImageButton, &QPushButton::clicked, this, &StarfieldTab::browseImage);
    connect(browseWcsButton, &QPushButton::clicked, this, &StarfieldTab::browseWcs);
    connect(browseGaiaButton, &QPushButton::clicked, this, &StarfieldTab::browseGaia);
    connect(fromProjectButton, &QPushButton::clicked, this, &StarfieldTab::fillFromProject);
    connect(epochOverrideCheck_, &QCheckBox::toggled, epochSpin_, &QWidget::setEnabled);
    connect(loadButton_, &QPushButton::clicked, this, &StarfieldTab::startLoad);
}

void StarfieldTab::fillFromProject()
{
    if (!projectBar_ || projectBar_->baseDir().isEmpty()) {
        summaryLabel_->setText(tr("Set a base directory in the Project bar above first."));
        return;
    }
    gaiaEdit_->setText(projectBar_->gaiaCsv());
}

void StarfieldTab::browseImage()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a solved image"), QString(),
                                                        tr("FITS images (*.fits *.fit *.fts);;All files (*)"));
    if (!file.isEmpty())
        imageEdit_->setText(file);
}

void StarfieldTab::browseWcs()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a .wcs file"), QString(),
                                                        tr("WCS files (*.wcs);;All files (*)"));
    if (!file.isEmpty())
        wcsEdit_->setText(file);
}

void StarfieldTab::browseGaia()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Choose a Gaia catalog CSV"), QString(),
                                                        tr("CSV files (*.csv);;All files (*)"));
    if (!file.isEmpty())
        gaiaEdit_->setText(file);
}

void StarfieldTab::setBusy(bool busy)
{
    loadButton_->setEnabled(!busy);
    loadButton_->setText(busy ? tr("Loading...") : tr("Load"));
}

void StarfieldTab::startLoad()
{
    if (imageEdit_->text().trimmed().isEmpty() || gaiaEdit_->text().trimmed().isEmpty()) {
        summaryLabel_->setText(tr("An image and a Gaia catalog CSV are both required."));
        return;
    }

    StarfieldWorker::Request request;
    request.imagePath = imageEdit_->text().trimmed();
    request.wcsPath = wcsEdit_->text().trimmed();
    request.gaiaCsvPath = gaiaEdit_->text().trimmed();
    request.topN = topNSpin_->value();
    request.epochOverrideJyear =
        epochOverrideCheck_->isChecked() ? epochSpin_->value() : std::numeric_limits<double>::quiet_NaN();

    summaryLabel_->setText(tr("Loading..."));
    setBusy(true);

    auto *worker = new StarfieldWorker(std::move(request));
    thread_ = new QThread(this);
    worker->moveToThread(thread_);
    connect(thread_, &QThread::started, worker, &StarfieldWorker::run);
    connect(worker, &StarfieldWorker::finished, this, &StarfieldTab::onWorkerFinished);
    connect(worker, &StarfieldWorker::finished, thread_, &QThread::quit);
    connect(thread_, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread_, &QThread::finished, thread_, &QObject::deleteLater);
    thread_->start();
}

void StarfieldTab::onWorkerFinished(StarfieldWorker::Result result)
{
    setBusy(false);
    thread_ = nullptr;

    if (!result.ok) {
        summaryLabel_->setText(tr("Error: %1").arg(result.errorMessage));
        canvas_->clear();
        return;
    }

    canvas_->setImage(result.stretchedPixels, result.imageWidth, result.imageHeight);
    canvas_->setArrows(result.arrows, result.circleRadiusPix);

    if (!epochOverrideCheck_->isChecked())
        epochSpin_->setValue(result.usedEpochJyear);

    QString text = tr("Epoch shown: %1 (jyear %2) -- %3 star(s) circled out of %4 in the Gaia "
                       "catalog, average line length %5 px (image diagonal / 30).")
                       .arg(jyearToDateString(result.usedEpochJyear))
                       .arg(QString::number(result.usedEpochJyear, 'f', 4))
                       .arg(result.arrows.size())
                       .arg(result.nCatalogStars)
                       .arg(QString::number(result.averageArrowLengthPix, 'f', 1));
    if (!result.warning.isEmpty())
        text += tr("\nNote: %1").arg(result.warning);
    summaryLabel_->setText(text);
}

} // namespace epochfrom::gui
