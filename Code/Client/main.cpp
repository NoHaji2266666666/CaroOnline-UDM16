#include "MainWindow.h"
#include <QApplication>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("CaroClient"));
    app.setOrganizationName(QStringLiteral("LTMang"));
    MainWindow window;
    window.show();
    return app.exec();
}
