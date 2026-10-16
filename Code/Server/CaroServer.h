#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QPointer>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QVector>
#include <QTimer>
#include <QSqlDatabase>
#include <QStringList>

class CaroServer : public QObject {
    Q_OBJECT
public:
    explicit CaroServer(QObject *parent = nullptr);
    ~CaroServer() override;
    bool start(const QString &address, quint16 port);

private:
    struct Session {
        QString name;
        QString token;
        QPointer<QTcpSocket> socket;
        qint64 offlineSince = 0;
        int roomId = -1;
        bool spectator = false;
    };
    struct Room {
        int id = 0;
        QString x, o;
        QVector<int> board = QVector<int>(225, 0);
        QStringList spectators;
        int turn = 1;
        int lastRow = -1, lastCol = -1;
        qint64 startedAt = 0, deadline = 0, endedAt = 0;
        qint64 pausedRemaining = 0;
        bool paused = false, active = true;
        int lastBroadcastSecond = -1;
        QString winner, reason;
        QJsonArray moves;
    };
    struct Invite { QString from, to; qint64 expiresAt = 0; };

    QTcpServer listener_;
    QHash<QTcpSocket *, QByteArray> input_;
    QHash<QString, Session> sessions_;
    QHash<int, Room> rooms_;
    QList<Invite> invites_;
    QTimer clock_;
    QSqlDatabase database_;
    int nextRoomId_ = 1;

    static qint64 nowMs();
    void log(const QString &message) const;
    bool openDatabase();
    void acceptConnection();
    void receive(QTcpSocket *socket);
    void disconnected(QTcpSocket *socket);
    void process(QTcpSocket *socket, const QJsonObject &msg);
    void hello(QTcpSocket *socket, const QJsonObject &msg);
    void send(QTcpSocket *socket, const QJsonObject &msg) const;
    void sendTo(const QString &user, const QJsonObject &msg) const;
    void error(QTcpSocket *socket, const QString &message) const;
    void notice(const QString &user, const QString &message) const;
    void broadcastUsers() const;
    void broadcastRooms() const;
    void sendHistory(const QString &user);
    bool userAvailable(const QString &name) const;
    void createMatch(const QString &x, const QString &o);
    bool hasWinner(const Room &room, int row, int col, int color) const;
    QJsonObject snapshot(const Room &room) const;
    void broadcastSnapshot(const Room &room) const;
    void pauseIfNeeded(Room &room);
    void finish(Room &room, const QString &winner, const QString &reason);
    void clockTick();
};
