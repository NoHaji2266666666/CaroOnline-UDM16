#include "CaroServer.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("CaroServer"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("UDM_16 - TCP Game Caro Server"));
    parser.addHelpOption();
    QCommandLineOption hostOption(QStringList{QStringLiteral("a"), QStringLiteral("address")},
                                   QStringLiteral("Bind address (default 0.0.0.0)"), QStringLiteral("address"),
                                   QStringLiteral("0.0.0.0"));
    QCommandLineOption portOption(QStringList{QStringLiteral("p"), QStringLiteral("port")},
                                   QStringLiteral("TCP port (default 45454)"), QStringLiteral("port"),
                                   QStringLiteral("45454"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.process(app);
    bool ok = false;
    const int port = parser.value(portOption).toInt(&ok);
    if (!ok || port < 1 || port > 65535) {
        QTextStream(stderr) << "Port must be in 1..65535\n";
        return 2;
    }
    CaroServer server;
    if (!server.start(parser.value(hostOption), static_cast<quint16>(port))) return 1;
    return app.exec();
}
