#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("EpochFrom");
    QCoreApplication::setApplicationName("EpochFrom-gui");
    QCoreApplication::setApplicationVersion("0.1.0");

    epochfrom::gui::MainWindow window;
    window.show();

    return app.exec();
}
