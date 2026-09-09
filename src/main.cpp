#include <QApplication>

#include "app/main_window.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("QtBlueprintGenerator"));
    QCoreApplication::setApplicationName(QStringLiteral("BlueprintEditor"));
    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
