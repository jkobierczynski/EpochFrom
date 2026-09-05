#include "GaiaTab.h"
#include "NoWheelWidgets.h"
#include "ProjectBar.h"
#include "TabLayoutHelpers.h"

#include <QCoreApplication>
#include <QDir>
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
#include <QSplitter>
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

GaiaTab::GaiaTab(ProjectBar *projectBar, QWidget *parent) : QWidget(parent), projectBar_(projectBar)
{
    fitsRadio_ = new QRadioButton(tr("From a FITS file's header (CRVAL1/2, RA/DEC, or OBJCTRA/DEC)"));
    raDecRadio_ = new QRadioButton(tr("Explicit RA/Dec"));
    presetRadio_ = new QRadioButton(tr("Known preset"));
    fitsRadio_->setChecked(true);

    fitsPathEdit_ = new QLineEdit;
    auto *browseFitsButton = new QPushButton(tr("Browse..."));

    raSpin_ = new NoWheelDoubleSpinBox;
    raSpin_->setRange(0.0, 360.0);
    raSpin_->setDecimals(6);
    raSpin_->setSuffix(tr(" deg RA"));
    raSpin_->setEnabled(false);
    decSpin_ = new NoWheelDoubleSpinBox;
    decSpin_->setRange(-90.0, 90.0);
    decSpin_->setDecimals(6);
    decSpin_->setSuffix(tr(" deg Dec"));
    decSpin_->setEnabled(false);

    presetCombo_ = new NoWheelComboBox;
    presetCombo_->addItems({QStringLiteral("northamerica"), QStringLiteral("pelican")});
    presetCombo_->setEnabled(false);

    auto *inputGroup = new QGroupBox(tr("Field center"));
    auto *inputForm = new QFormLayout(inputGroup);
    inputForm->addRow(fitsRadio_, rowOf({fitsPathEdit_, browseFitsButton}));
    inputForm->addRow(raDecRadio_, rowOf({raSpin_, decSpin_}));
    inputForm->addRow(presetRadio_, presetCombo_);

    radiusSpin_ = new NoWheelDoubleSpinBox;
    radiusSpin_->setRange(0.01, 90.0);
    radiusSpin_->setDecimals(3);
    radiusSpin_->setValue(0.9);
    radiusSpin_->setSuffix(tr(" deg radius"));
    maglimSpin_ = new NoWheelDoubleSpinBox;
    maglimSpin_->setRange(1.0, 25.0);
    maglimSpin_->setDecimals(2);
    maglimSpin_->setValue(16.0);
    maglimSpin_->setSuffix(tr(" G mag limit"));
    maxRuweSpin_ = new NoWheelDoubleSpinBox;
    maxRuweSpin_->setRange(0.1, 100.0);
    maxRuweSpin_->setDecimals(2);
    maxRuweSpin_->setValue(1.4);
    maxRuweSpin_->setSuffix(tr(" max RUWE"));
    limitSpin_ = new NoWheelSpinBox;
    limitSpin_->setRange(1, 2000000);
    limitSpin_->setValue(50000);
    limitSpin_->setSuffix(tr(" row cap"));

    auto *queryGroup = new QGroupBox(tr("Query options"));
    auto *queryForm = new QFormLayout(queryGroup);
    queryForm->addRow(tr("Search:"), rowOf({radiusSpin_, maglimSpin_}));
    queryForm->addRow(tr("Quality:"), rowOf({maxRuweSpin_, limitSpin_}));

    outPathEdit_ = new QLineEdit;
    auto *browseOutButton = new QPushButton(tr("Browse..."));
    auto *fromProjectButton = new QPushButton(tr("Fill from Project"));
    fromProjectButton->setToolTip(
        tr("Use the project bar's base directory as this catalog's save location "
           "(shared across every filter -- the star field doesn't change with filter)"));
    pythonPathEdit_ = new QLineEdit(QStringLiteral("python3"));
    scriptPathEdit_ = new QLineEdit;
    scriptPathEdit_->setPlaceholderText(tr("auto-detected next to this application"));
    auto *browseScriptButton = new QPushButton(tr("Browse..."));

    auto *outputGroup = new QGroupBox(tr("Output"));
    auto *outputForm = new QFormLayout(outputGroup);
    outputForm->addRow(QString(), fromProjectButton);
    outputForm->addRow(tr("Save catalog as:"), rowOf({outPathEdit_, browseOutButton}));
    outputForm->addRow(tr("Python interpreter:"), pythonPathEdit_);
    outputForm->addRow(tr("gaia_field_query.py:"), rowOf({scriptPathEdit_, browseScriptButton}));

    queryButton_ = new QPushButton(tr("Query Gaia"));
    cancelButton_ = new QPushButton(tr("Cancel"));
    cancelButton_->setEnabled(false);
    logView_ = new QPlainTextEdit;
    logView_->setReadOnly(true);
    logView_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono(QStringLiteral("monospace"));
    mono.setStyleHint(QFont::Monospace);
    logView_->setFont(mono);

    auto *optionsColumn = new QWidget;
    auto *optionsLayout = new QVBoxLayout(optionsColumn);
    optionsLayout->addWidget(inputGroup);
    optionsLayout->addWidget(queryGroup);
    optionsLayout->addWidget(outputGroup);
    optionsLayout->addStretch();

    auto *scrollArea = new QScrollArea;
    scrollArea->setWidget(optionsColumn);
    scrollArea->setWidgetResizable(true);

    // See SolveTab.cpp for why the options and the output share a
    // QSplitter instead of a flat, all-scrolling column.
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(scrollArea);
    splitter->addWidget(logView_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({1, 1});

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addWidget(makeActionBar(splitter, queryButton_, {cancelButton_}));

    auto updateInputEnabled = [this]() {
        fitsPathEdit_->setEnabled(fitsRadio_->isChecked());
        raSpin_->setEnabled(raDecRadio_->isChecked());
        decSpin_->setEnabled(raDecRadio_->isChecked());
        presetCombo_->setEnabled(presetRadio_->isChecked());
    };
    updateInputEnabled();
    connect(fitsRadio_, &QRadioButton::toggled, this, updateInputEnabled);
    connect(raDecRadio_, &QRadioButton::toggled, this, updateInputEnabled);
    connect(presetRadio_, &QRadioButton::toggled, this, updateInputEnabled);

    connect(browseFitsButton, &QPushButton::clicked, this, &GaiaTab::browseFits);
    connect(browseOutButton, &QPushButton::clicked, this, &GaiaTab::browseOut);
    connect(browseScriptButton, &QPushButton::clicked, this, [this]() {
        const QString file = QFileDialog::getOpenFileName(this, tr("Locate gaia_field_query.py"),
                                                            QString(), tr("Python scripts (*.py)"));
        if (!file.isEmpty())
            scriptPathEdit_->setText(file);
    });
    connect(fromProjectButton, &QPushButton::clicked, this, &GaiaTab::fillFromProject);
    connect(queryButton_, &QPushButton::clicked, this, &GaiaTab::startQuery);
    connect(cancelButton_, &QPushButton::clicked, this, &GaiaTab::cancelQuery);

    const QString detected = findScript();
    if (!detected.isEmpty())
        scriptPathEdit_->setText(detected);
}

GaiaTab::~GaiaTab()
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(3000);
    }
}

QString GaiaTab::findScript() const
{
    // scripts/gaia_field_query.py isn't installed anywhere fixed (there's no
    // `install` target in this early-scaffold project yet) -- look in the
    // handful of places it plausibly sits relative to wherever this binary
    // happens to be running from, same approach as
    // MainWindow::findResidualFieldViewer().
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        QDir(appDir).filePath("scripts/gaia_field_query.py"),
        QDir(appDir).filePath("../scripts/gaia_field_query.py"),
        QDir(appDir).filePath("../../scripts/gaia_field_query.py"),
        QDir(appDir).filePath("../../../scripts/gaia_field_query.py"),
        QDir::current().filePath("scripts/gaia_field_query.py"),
    };
    for (const QString &candidate : candidates) {
        const QString cleaned = QDir::cleanPath(candidate);
        if (QFileInfo::exists(cleaned))
            return cleaned;
    }
    return QString();
}

void GaiaTab::browseFits()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Choose a FITS image"), QString(),
        tr("FITS images (*.fits *.fit *.fts);;All files (*)"));
    if (!file.isEmpty())
        fitsPathEdit_->setText(file);
}

void GaiaTab::browseOut()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Save Gaia catalog"), QString(),
                                                        tr("CSV files (*.csv);;All files (*)"));
    if (!file.isEmpty())
        outPathEdit_->setText(file);
}

void GaiaTab::fillFromProject()
{
    if (!projectBar_ || projectBar_->baseDir().isEmpty()) {
        appendLog(tr("Set a base directory in the Project bar above first.\n"));
        return;
    }
    outPathEdit_->setText(projectBar_->gaiaCsv());
}

void GaiaTab::setBusy(bool busy)
{
    queryButton_->setEnabled(!busy);
    queryButton_->setText(busy ? tr("Querying...") : tr("Query Gaia"));
    cancelButton_->setEnabled(busy);
}

void GaiaTab::startQuery()
{
    if (outPathEdit_->text().trimmed().isEmpty()) {
        appendLog(tr("Choose where to save the catalog CSV first.\n"));
        return;
    }
    const QString script = scriptPathEdit_->text().trimmed();
    if (script.isEmpty() || !QFileInfo::exists(script)) {
        appendLog(tr("Can't find gaia_field_query.py -- browse to it manually (it lives under "
                      "scripts/ in this project's repository).\n"));
        return;
    }
    if (fitsRadio_->isChecked() && fitsPathEdit_->text().trimmed().isEmpty()) {
        appendLog(tr("Choose a FITS file to read the field center from, or pick another input mode.\n"));
        return;
    }

    QStringList args;
    args << script;
    if (fitsRadio_->isChecked()) {
        args << "--fits" << fitsPathEdit_->text().trimmed();
    } else if (raDecRadio_->isChecked()) {
        args << "--ra" << QString::number(raSpin_->value(), 'f', 6)
             << "--dec" << QString::number(decSpin_->value(), 'f', 6);
    } else {
        args << "--target" << presetCombo_->currentText();
    }
    args << "--radius" << QString::number(radiusSpin_->value(), 'f', 4)
         << "--maglim" << QString::number(maglimSpin_->value(), 'f', 3)
         << "--max-ruwe" << QString::number(maxRuweSpin_->value(), 'f', 3)
         << "--limit" << QString::number(limitSpin_->value())
         << "--out" << outPathEdit_->text().trimmed();

    logView_->clear();
    setBusy(true);
    appendLog(tr("Running: %1 %2\n\n").arg(pythonPathEdit_->text().trimmed(), args.join(' ')));

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyReadStandardOutput, this, &GaiaTab::onReadyRead);
    connect(process_, &QProcess::finished, this, &GaiaTab::onProcessFinished);
    connect(process_, &QProcess::errorOccurred, this, &GaiaTab::onProcessErrorOccurred);
    process_->start(pythonPathEdit_->text().trimmed(), args);
}

void GaiaTab::cancelQuery()
{
    if (process_ && process_->state() != QProcess::NotRunning) {
        appendLog(tr("\nCancelling...\n"));
        process_->kill();
    }
}

void GaiaTab::onReadyRead()
{
    if (process_)
        appendLog(QString::fromLocal8Bit(process_->readAllStandardOutput()));
}

void GaiaTab::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (status == QProcess::CrashExit)
        appendLog(tr("\nQuery was cancelled or crashed.\n"));
    else if (exitCode != 0)
        appendLog(tr("\nQuery exited with code %1.\n").arg(exitCode));
    else
        appendLog(tr("\nDone.\n"));
    setBusy(false);
    if (process_) {
        process_->deleteLater();
        process_ = nullptr;
    }
}

void GaiaTab::onProcessErrorOccurred(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        appendLog(tr("\nCouldn't start '%1' -- check the Python interpreter path.\n")
                      .arg(pythonPathEdit_->text().trimmed()));
        setBusy(false);
    }
}

void GaiaTab::appendLog(const QString &text)
{
    logView_->insertPlainText(text);
    logView_->verticalScrollBar()->setValue(logView_->verticalScrollBar()->maximum());
}

} // namespace epochfrom::gui
