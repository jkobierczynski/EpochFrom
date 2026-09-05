#pragma once

#include <QWidget>

class QLineEdit;

namespace epochfrom::gui {

class NoWheelComboBox;

// A persistent strip that sits above the tab widget in MainWindow, holding
// settings shared across every tab: a project's base directory, which
// filter (Ha/OIII/SII/L/R/G/B/... or none) is currently being processed,
// and how that filter maps onto actual subdirectory/file names -- not
// every rig's capture software names a filter's session folder exactly
// "Ha"; some produce something like "Light_Ha_600_secs" instead. The dir
// and residuals-filename fields are patterns where every "%filter%" is
// replaced with the filter above (defaults: "%filter%" and
// "%filter%_residuals.csv", matching the old fixed behavior). All four
// fields are saved to QSettings and restored on next launch.
//
// Tabs don't read these live -- each has its own "Fill from Project"
// button that pulls the current base dir/filter through ProjectPaths.h and
// writes the composed paths into its own fields once, on click. That keeps
// the fields ordinary and freely editable afterward, rather than fields
// that mysteriously change out from under the user whenever the project
// bar changes.
class ProjectBar : public QWidget {
    Q_OBJECT
public:
    explicit ProjectBar(QWidget *parent = nullptr);

    QString baseDir() const;
    QString filter() const;

    // Convenience wrappers around ProjectPaths.h using this bar's current
    // base directory and filter.
    QString subsDir() const;
    QString gaiaCsv() const;
    QString profilePath() const;
    QString residualsCsv() const;

signals:
    void baseDirChanged(const QString &baseDir);
    void filterChanged(const QString &filter);

private slots:
    void browseBaseDir();

private:
    void loadSettings();

    QLineEdit *baseDirEdit_;
    NoWheelComboBox *filterCombo_;
    QLineEdit *dirPatternEdit_;
    QLineEdit *residualsPatternEdit_;
};

} // namespace epochfrom::gui
