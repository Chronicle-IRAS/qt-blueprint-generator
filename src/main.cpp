#include <QApplication>

#include "app/main_window.h"
#include "ui/theme.h"

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    EditorTheme::apply(application);
    QCoreApplication::setOrganizationName(QStringLiteral("QtBlueprintGenerator"));
    QCoreApplication::setApplicationName(QStringLiteral("BlueprintEditor"));
    MainWindow mainWindow;
    mainWindow.show();

    return application.exec();
}
