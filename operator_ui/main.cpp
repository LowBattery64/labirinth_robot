#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    application.setOrganizationName("LowBattery64");
    application.setApplicationName("OmegaBotOperator");

    MainWindow window;
    window.show();

    return application.exec();
}
