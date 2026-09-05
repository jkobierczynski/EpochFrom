#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("EpochFrom");
    QCoreApplication::setApplicationVersion("0.1.0");

    epochfrom::gui::MainWindow window;
    window.show();

    return app.exec();
}
