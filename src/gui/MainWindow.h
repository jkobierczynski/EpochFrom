#pragma once

#include <QMainWindow>

namespace epochfrom::gui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void openResidualFieldViewer();
    void showAbout();

private:
    QString findResidualFieldViewer() const;
};

} // namespace epochfrom::gui
