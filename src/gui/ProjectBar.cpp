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

    auto *group = new QGroupBox(tr("Project"));
    auto *layout = new QHBoxLayout(group);
    layout->addWidget(new QLabel(tr("Base directory:")));
    layout->addWidget(baseDirEdit_, 1);
    layout->addWidget(browseButton);
    layout->addWidget(new QLabel(tr("Filter:")));
    layout->addWidget(filterCombo_);

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
}

void ProjectBar::loadSettings()
{
    QSettings settings;
    baseDirEdit_->setText(settings.value(QStringLiteral("Project/BaseDir")).toString());
    filterCombo_->setCurrentText(settings.value(QStringLiteral("Project/Filter")).toString());
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
    return projectSubsDir(baseDir(), filter());
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
    return projectResidualsCsv(baseDir(), filter());
}

} // namespace epochfrom::gui
