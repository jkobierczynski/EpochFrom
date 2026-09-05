#include "ProjectBar.h"
#include "NoWheelWidgets.h"
#include "ProjectPaths.h"

#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace epochfrom::gui {

ProjectBar::ProjectBar(QWidget *parent) : QWidget(parent)
{
    baseDirEdit_ = new QLineEdit;
    baseDirEdit_->setPlaceholderText(
        tr("e.g. ~/astro/2026-08-14 North America Nebula -- shared by every filter below"));
    auto *browseButton = new QPushButton(tr("Browse..."));

    filterCombo_ = new NoWheelComboBox;
    filterCombo_->setEditable(true);
    filterCombo_->addItems({QString(), QStringLiteral("Ha"), QStringLiteral("OIII"),
                             QStringLiteral("SII"), QStringLiteral("L"), QStringLiteral("R"),
                             QStringLiteral("G"), QStringLiteral("B"), QStringLiteral("Lum")});
    filterCombo_->setCurrentText(QString());
    filterCombo_->setToolTip(
        tr("Which filter's subs you're processing right now -- pick a preset or type your own. "
           "Leave blank if this rig doesn't shoot separate filters."));

    dirPatternEdit_ = new QLineEdit(QStringLiteral("%filter%"));
    dirPatternEdit_->setFixedWidth(160);
    dirPatternEdit_->setToolTip(
        tr("Pattern for the subs subdirectory name under the base directory. Every \"%filter%\" "
           "is replaced with the filter above -- default \"%filter%\" means the subdirectory IS "
           "the filter name (e.g. <base>/Ha/). If your capture software names session folders "
           "something like \"Light_Ha_600_secs\" instead, set this to "
           "\"Light_%filter%_600_secs\" rather than renaming folders to match."));
    residualsPatternEdit_ = new QLineEdit(QStringLiteral("%filter%_residuals.csv"));
    residualsPatternEdit_->setFixedWidth(160);
    residualsPatternEdit_->setToolTip(
        tr("Pattern for the residuals CSV filename `calibrate` writes per filter (same "
           "\"%filter%\" substitution as the subs dir pattern). Default "
           "\"%filter%_residuals.csv\"."));

    auto *group = new QGroupBox(tr("Project"));
    auto *rowLayout = new QHBoxLayout;
    rowLayout->addWidget(new QLabel(tr("Base directory:")));
    rowLayout->addWidget(baseDirEdit_, 1);
    rowLayout->addWidget(browseButton);
    rowLayout->addWidget(new QLabel(tr("Filter:")));
    rowLayout->addWidget(filterCombo_);
    auto *patternRow = new QHBoxLayout;
    patternRow->addWidget(new QLabel(tr("Subs dir pattern:")));
    patternRow->addWidget(dirPatternEdit_);
    patternRow->addWidget(new QLabel(tr("Residuals filename pattern:")));
    patternRow->addWidget(residualsPatternEdit_);
    patternRow->addStretch();
    auto *layout = new QVBoxLayout(group);
    layout->addLayout(rowLayout);
    layout->addLayout(patternRow);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(group);

    loadSettings();

    connect(browseButton, &QPushButton::clicked, this, &ProjectBar::browseBaseDir);
    connect(baseDirEdit_, &QLineEdit::textChanged, this, [this](const QString &text) {
        QSettings settings;
        settings.setValue(QStringLiteral("Project/BaseDir"), text);
        emit baseDirChanged(text);
    });
    connect(filterCombo_, &QComboBox::currentTextChanged, this, [this](const QString &text) {
        QSettings settings;
        settings.setValue(QStringLiteral("Project/Filter"), text);
        emit filterChanged(text);
    });
    connect(dirPatternEdit_, &QLineEdit::textChanged, this, [](const QString &text) {
        QSettings settings;
        settings.setValue(QStringLiteral("Project/DirPattern"), text);
    });
    connect(residualsPatternEdit_, &QLineEdit::textChanged, this, [](const QString &text) {
        QSettings settings;
        settings.setValue(QStringLiteral("Project/ResidualsPattern"), text);
    });
}

void ProjectBar::loadSettings()
{
    QSettings settings;
    baseDirEdit_->setText(settings.value(QStringLiteral("Project/BaseDir")).toString());
    filterCombo_->setCurrentText(settings.value(QStringLiteral("Project/Filter")).toString());
    dirPatternEdit_->setText(
        settings.value(QStringLiteral("Project/DirPattern"), QStringLiteral("%filter%")).toString());
    residualsPatternEdit_->setText(
        settings.value(QStringLiteral("Project/ResidualsPattern"), QStringLiteral("%filter%_residuals.csv"))
            .toString());
}

void ProjectBar::browseBaseDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose a base directory"), baseDir());
    if (!dir.isEmpty())
        baseDirEdit_->setText(dir);
}

QString ProjectBar::baseDir() const
{
    return baseDirEdit_->text().trimmed();
}

QString ProjectBar::filter() const
{
    return filterCombo_->currentText().trimmed();
}

QString ProjectBar::subsDir() const
{
    return projectSubsDir(baseDir(), filter(), dirPatternEdit_->text());
}

QString ProjectBar::gaiaCsv() const
{
    return projectGaiaCsv(baseDir());
}

QString ProjectBar::profilePath() const
{
    return projectProfilePath(baseDir());
}

QString ProjectBar::residualsCsv() const
{
    return projectResidualsCsv(baseDir(), filter(), residualsPatternEdit_->text());
}

} // namespace epochfrom::gui
