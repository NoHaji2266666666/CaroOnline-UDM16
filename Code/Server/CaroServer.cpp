#include "CaroServer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTextStream>
#include <QUuid>
#include <QtGlobal>
#include <algorithm>

namespace {
constexpr int kSize = 15;
constexpr int kTurnSeconds = 30;
constexpr qint64 kGraceMs = 90000;
constexpr qint64 kInvitationMs = 60000;
constexpr qint64 kFrameMax = 65536;
QString appDir() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (path.isEmpty()) path = QDir::currentPath();
    QDir().mkpath(path);
    return path;
}
QString timestamp(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC).toString(Qt::ISODate);
}
}

CaroServer::CaroServer(QObject *parent) : QObject(parent) {
    connect(&listener_, &QTcpServer::newConnection, this, &CaroServer::acceptConnection);
    connect(&clock_, &QTimer::timeout, this, &CaroServer::clockTick);
    clock_.setInterval(250);
}

CaroServer::~CaroServer() {
    clock_.stop();
    listener_.close();
    if (database_.isOpen()) database_.close();
    database_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(QStringLiteral("caro_main"));
}

qint64 CaroServer::nowMs() { return QDateTime::currentMSecsSinceEpoch(); }
void CaroServer::log(const QString &message) const {
    const QString line = timestamp(nowMs()) + QStringLiteral(" | ") + message + QStringLiteral("\n");
    QTextStream(stdout) << line;
    QFile file(appDir() + QStringLiteral("/server.log"));
    if (file.open(QIODevice::Append | QIODevice::Text)) file.write(line.toUtf8());
}
bool CaroServer::openDatabase() {
    database_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("caro_main"));
    database_.setDatabaseName(appDir() + QStringLiteral("/matches.sqlite"));
    if (!database_.open()) { log(QStringLiteral("DATABASE ERROR: ") + database_.lastError().text()); return false; }
    QSqlQuery query(database_);
    if (!query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS matches ("
         "id INTEGER PRIMARY KEY AUTOINCREMENT, room_id INTEGER NOT NULL, "
         "player_x TEXT NOT NULL, player_o TEXT NOT NULL, winner TEXT NOT NULL, "
         "reason TEXT NOT NULL, started_at TEXT NOT NULL, ended_at TEXT NOT NULL, "
         "moves_json TEXT NOT NULL)"))) {
        log(QStringLiteral("SCHEMA ERROR: ") + query.lastError().text());
        return false;
    }
    QSqlQuery roomQuery(database_);

if (!roomQuery.exec(
        QStringLiteral("SELECT COALESCE(MAX(room_id), 0) FROM matches"))) {
    log(QStringLiteral("ROOM ID ERROR: ") + roomQuery.lastError().text());
    return false;
}

if (roomQuery.next()) {
    nextRoomId_ = roomQuery.value(0).toInt() + 1;
}
    return true;
}
bool CaroServer::start(const QString &address, quint16 port) {
    if (!openDatabase()) return false;
    QHostAddress bindAddress;
    if (!bindAddress.setAddress(address)) { log(QStringLiteral("Invalid bind address: ") + address); return false; }
    if (!listener_.listen(bindAddress, port)) { log(QStringLiteral("LISTEN ERROR: ") + listener_.errorString()); return false; }
    log(QStringLiteral("LISTEN %1:%2 | database: %3").arg(address).arg(port).arg(database_.databaseName()));
    clock_.start();
    return true;
}
void CaroServer::acceptConnection() {
    while (listener_.hasPendingConnections()) {
        QTcpSocket *socket = listener_.nextPendingConnection();
        if (!socket) break;
        input_.insert(socket, QByteArray());
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { receive(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { disconnected(socket); socket->deleteLater(); });
        log(QStringLiteral("CONNECTED %1").arg(socket->peerAddress().toString()));
    }
}
void CaroServer::send(QTcpSocket *socket, const QJsonObject &msg) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    QByteArray bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    socket->write(bytes);
}
void CaroServer::sendTo(const QString &user, const QJsonObject &msg) const {
    const auto it = sessions_.constFind(user);
    if (it != sessions_.cend()) send(it->socket, msg);
}
void CaroServer::error(QTcpSocket *socket, const QString &message) const {
    send(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("error")}, {QStringLiteral("message"), message}});
}
void CaroServer::notice(const QString &user, const QString &message) const {
    sendTo(user, QJsonObject{{QStringLiteral("type"), QStringLiteral("notice")}, {QStringLiteral("message"), message}});
}
void CaroServer::receive(QTcpSocket *socket) {
    if (!input_.contains(socket)) return;
    QByteArray &buffer = input_[socket];
    buffer.append(socket->readAll());
    if (buffer.size() > kFrameMax * 2) {
        error(socket, QStringLiteral("Dữ liệu vượt giới hạn.")); socket->disconnectFromHost(); return;
    }
    while (true) {
        int end = buffer.indexOf('\n');
        if (end < 0) break;
        if (end > kFrameMax) { error(socket, QStringLiteral("Gói quá lớn.")); socket->disconnectFromHost(); return; }
        QByteArray frame = buffer.left(end);
        buffer.remove(0, end + 1);
        QJsonParseError parseError;
        QJsonDocument document = QJsonDocument::fromJson(frame, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            error(socket, QStringLiteral("JSON không hợp lệ."));
            continue;
        }
        process(socket, document.object());
        if (socket->state() != QAbstractSocket::ConnectedState) return;
    }
    if (buffer.size() > kFrameMax) { error(socket, QStringLiteral("Gói quá lớn.")); socket->disconnectFromHost(); }
}
void CaroServer::disconnected(QTcpSocket *socket) {
    input_.remove(socket);
    QString user;
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
        if (it->socket == socket) { user = it.key(); break; }
    }
    if (user.isEmpty()) return;
    auto it = sessions_.find(user);
    it->socket = nullptr;
    it->offlineSince = nowMs();
    const int roomId = it->roomId;
    if (roomId >= 0 && rooms_.contains(roomId)) {
        Room &room = rooms_[roomId];
        if (it->spectator) {
            room.spectators.removeAll(user);
            it->roomId = -1;
            it->spectator = false;
            broadcastSnapshot(room);
        } else if (room.active) {
            pauseIfNeeded(room);
            broadcastSnapshot(room);
        }
    }
    log(QStringLiteral("DISCONNECTED %1").arg(user));
    broadcastUsers();
    broadcastRooms();
}
void CaroServer::hello(QTcpSocket *socket, const QJsonObject &msg) {
    const QString name = msg.value(QStringLiteral("name")).toString().trimmed();
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9_]{3,20}$"));
    if (!valid.match(name).hasMatch()) { error(socket, QStringLiteral("Tên: 3-20 ký tự tiếng Anh, số hoặc dấu _.")); return; }
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (it->socket == socket) { error(socket, QStringLiteral("Đã đăng nhập trên kết nối này.")); return; }
    }
    bool resumed = false;
    if (sessions_.contains(name)) {
        Session &old = sessions_[name];
        if (old.socket) { error(socket, QStringLiteral("Tên đã được sử dụng online.")); return; }
        const QString token = msg.value(QStringLiteral("token")).toString();
        if (token.isEmpty() || token != old.token) { error(socket, QStringLiteral("Tên đang được giữ cho phiên reconnect khác.")); return; }
        old.socket = socket;
        old.offlineSince = 0;
        resumed = true;
    } else {
        Session session;
        session.name = name;
        session.token = QUuid::createUuid().toString(QUuid::WithoutBraces)
                        + QUuid::createUuid().toString(QUuid::WithoutBraces);
        session.socket = socket;
        sessions_.insert(name, session);
    }
    Session &session = sessions_[name];
    send(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("welcome")},
        {QStringLiteral("name"), name}, {QStringLiteral("token"), session.token}, {QStringLiteral("resumed"), resumed}});
    if (session.roomId >= 0 && rooms_.contains(session.roomId)) {
        Room &room = rooms_[session.roomId];
        pauseIfNeeded(room);
        send(socket, snapshot(room));
        broadcastSnapshot(room);
    }
    sendHistory(name);
    log(QStringLiteral("LOGIN %1 (resumed=%2)").arg(name).arg(resumed));
    broadcastUsers();
    broadcastRooms();
}
bool CaroServer::userAvailable(const QString &name) const {
    auto it = sessions_.constFind(name);
    return it != sessions_.cend() && it->socket && it->roomId < 0;
}
void CaroServer::broadcastUsers() const {
    QStringList keys = sessions_.keys(); keys.sort(Qt::CaseInsensitive);
    QJsonArray array;
    for (const auto &name : keys) {
        const auto &session = sessions_[name];
        if (session.socket)
            array.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("busy"), session.roomId >= 0}});
    }
    QJsonObject obj{{QStringLiteral("type"), QStringLiteral("users")}, {QStringLiteral("users"), array}};
    for (const auto &session : sessions_) send(session.socket, obj);
}
void CaroServer::broadcastRooms() const {
    QList<int> ids = rooms_.keys(); std::sort(ids.begin(), ids.end());
    QJsonArray array;
    for (int id : ids) {
        const Room &room = rooms_[id];
        if (!room.active) continue;
        array.append(QJsonObject{{QStringLiteral("id"), id}, {QStringLiteral("x"), room.x},
          {QStringLiteral("o"), room.o}, {QStringLiteral("spectators"), room.spectators.size()},
          {QStringLiteral("paused"), room.paused}});
    }
    QJsonObject obj{{QStringLiteral("type"), QStringLiteral("rooms")}, {QStringLiteral("rooms"), array}};
    for (const auto &session : sessions_) send(session.socket, obj);
}
void CaroServer::sendHistory(const QString &user) {
    QJsonArray entries;
    QSqlQuery query(database_);
    if (query.exec(QStringLiteral("SELECT room_id, player_x, player_o, winner, reason, ended_at "
                                  "FROM matches ORDER BY id DESC LIMIT 50"))) {
        while (query.next()) {
            entries.append(QJsonObject{{QStringLiteral("room"), query.value(0).toInt()},
               {QStringLiteral("x"), query.value(1).toString()}, {QStringLiteral("o"), query.value(2).toString()},
               {QStringLiteral("winner"), query.value(3).toString()}, {QStringLiteral("reason"), query.value(4).toString()},
               {QStringLiteral("endedAt"), query.value(5).toString()}});
        }
    }
    sendTo(user, QJsonObject{{QStringLiteral("type"), QStringLiteral("history_result")},
                             {QStringLiteral("matches"), entries}});
}
void CaroServer::createMatch(const QString &x, const QString &o) {
    if (!userAvailable(x) || !userAvailable(o)) return;
    Room room;
    room.id = nextRoomId_++;
    room.x = x; room.o = o;
    room.startedAt = nowMs();
    room.deadline = room.startedAt + kTurnSeconds * 1000;
    sessions_[x].roomId = room.id;
    sessions_[o].roomId = room.id;
    rooms_.insert(room.id, room);
    log(QStringLiteral("MATCH_START #%1 X=%2 O=%3").arg(room.id).arg(x, o));
    broadcastSnapshot(rooms_[room.id]);
    broadcastUsers(); broadcastRooms();
}
bool CaroServer::hasWinner(const Room &room, int row, int col, int color) const {
    constexpr int dirs[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
    for (auto &d : dirs) {
        int count = 1;
        for (int side : {-1, 1}) {
            int r = row + d[0] * side, c = col + d[1] * side;
            while (r >= 0 && r < kSize && c >= 0 && c < kSize && room.board[r * kSize + c] == color) {
                ++count; r += d[0] * side; c += d[1] * side;
            }
        }
        if (count >= 5) return true;
    }
    return false;
}
QJsonObject CaroServer::snapshot(const Room &room) const {
    QJsonArray board;
    for (int r = 0; r < kSize; ++r) {
        QString line; line.reserve(kSize);
        for (int c = 0; c < kSize; ++c) {
            const int v = room.board[r*kSize+c];
            line.append(v == 1 ? QLatin1Char('X') : (v == 2 ? QLatin1Char('O') : QLatin1Char('.')));
        }
        board.append(line);
    }
    const qint64 left = room.paused ? room.pausedRemaining : room.deadline - nowMs();
    const bool xOnline = sessions_.contains(room.x) && sessions_[room.x].socket;
    const bool oOnline = sessions_.contains(room.o) && sessions_[room.o].socket;
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("snapshot")},
        {QStringLiteral("room"), room.id}, {QStringLiteral("x"), room.x}, {QStringLiteral("o"), room.o},
        {QStringLiteral("board"), board}, {QStringLiteral("turn"), room.turn == 1 ? QStringLiteral("X") : QStringLiteral("O")},
        {QStringLiteral("secondsLeft"), room.active ? int(std::max<qint64>(0, (left + 999) / 1000)) : 0},
        {QStringLiteral("paused"), room.paused}, {QStringLiteral("onlineX"), xOnline},
        {QStringLiteral("onlineO"), oOnline}, {QStringLiteral("active"), room.active},
        {QStringLiteral("winner"), room.winner}, {QStringLiteral("reason"), room.reason},
        {QStringLiteral("spectators"), room.spectators.size()},
        {QStringLiteral("lastRow"), room.lastRow}, {QStringLiteral("lastCol"), room.lastCol}};
}
void CaroServer::broadcastSnapshot(const Room &room) const {
    const QJsonObject msg = snapshot(room);
    sendTo(room.x, msg); sendTo(room.o, msg);
    for (const QString &name : room.spectators) sendTo(name, msg);
}
void CaroServer::pauseIfNeeded(Room &room) {
    if (!room.active) return;
    const bool allOnline = sessions_[room.x].socket && sessions_[room.o].socket;
    if (!allOnline && !room.paused) {
        room.pausedRemaining = std::max<qint64>(1000, room.deadline - nowMs());
        room.paused = true;
        log(QStringLiteral("MATCH_PAUSED #%1").arg(room.id));
    } else if (allOnline && room.paused) {
        room.deadline = nowMs() + room.pausedRemaining;
        room.paused = false;
        log(QStringLiteral("MATCH_RESUMED #%1").arg(room.id));
    }
}
void CaroServer::finish(Room &room, const QString &winner, const QString &reason) {
    if (!room.active) return;
    room.active = false;
    room.endedAt = nowMs();
    room.winner = winner;
    room.reason = reason;
    QSqlQuery query(database_);
    query.prepare(QStringLiteral("INSERT INTO matches (room_id,player_x,player_o,winner,reason,started_at,ended_at,moves_json) "
                                 "VALUES (?,?,?,?,?,?,?,?)"));
    query.addBindValue(room.id);
    query.addBindValue(room.x);
    query.addBindValue(room.o);
    query.addBindValue(winner);
    query.addBindValue(reason);
    query.addBindValue(timestamp(room.startedAt));
    query.addBindValue(timestamp(room.endedAt));
    query.addBindValue(QString::fromUtf8(QJsonDocument(room.moves).toJson(QJsonDocument::Compact)));
    if (!query.exec()) log(QStringLiteral("DATABASE SAVE ERROR: ") + query.lastError().text());
    broadcastSnapshot(room);
    for (const QString &name : QStringList{room.x, room.o} + room.spectators) {
        if (sessions_.contains(name) && sessions_[name].roomId == room.id) {
            sessions_[name].roomId = -1;
            sessions_[name].spectator = false;
        }
    }
    log(QStringLiteral("MATCH_END #%1 winner=%2 reason=%3").arg(room.id).arg(winner, reason));
    broadcastUsers(); broadcastRooms();
    for (const QString &name : QStringList{room.x, room.o} + room.spectators) sendHistory(name);
}
void CaroServer::process(QTcpSocket *socket, const QJsonObject &msg) {
    const QString type = msg.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("hello")) { hello(socket, msg); return; }
    QString me;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (it->socket == socket) { me = it.key(); break; }
    }
    if (me.isEmpty()) { error(socket, QStringLiteral("Cần đăng nhập trước.")); return; }
    if (type == QStringLiteral("list_users")) { broadcastUsers(); return; }
    if (type == QStringLiteral("list_rooms")) { broadcastRooms(); return; }
    if (type == QStringLiteral("history")) { sendHistory(me); return; }
    if (type == QStringLiteral("invite")) {
        QString target = msg.value(QStringLiteral("target")).toString();
        if (target == me || !userAvailable(me) || !userAvailable(target)) {
            error(socket, QStringLiteral("Không thể mời người này lúc này.")); return;
        }
        for (const auto &invite : invites_) {
            if (invite.from == me && invite.to == target) { error(socket, QStringLiteral("Đã gửi lời mời, hãy chờ phản hồi.")); return; }
        }
        invites_.append(Invite{me, target, nowMs() + kInvitationMs});
        sendTo(target, QJsonObject{{QStringLiteral("type"), QStringLiteral("invite_received")}, {QStringLiteral("from"), me}});
        notice(me, QStringLiteral("Đã mời %1 (hiệu lực 60 giây).").arg(target));
        return;
    }
    if (type == QStringLiteral("invite_reply")) {
        const QString from = msg.value(QStringLiteral("from")).toString();
        if (!msg.value(QStringLiteral("accept")).isBool()) { error(socket, QStringLiteral("Phản hồi không hợp lệ.")); return; }
        int index = -1;
        for (int i = 0; i < invites_.size(); ++i) {
            if (invites_[i].from == from && invites_[i].to == me && invites_[i].expiresAt > nowMs()) { index = i; break; }
        }
        if (index < 0) { error(socket, QStringLiteral("Lời mời đã hết hạn hoặc không tồn tại.")); return; }
        invites_.removeAt(index);
        const bool accept = msg.value(QStringLiteral("accept")).toBool();
        sendTo(from, QJsonObject{{QStringLiteral("type"), QStringLiteral("invite_result")},
                                 {QStringLiteral("target"), me}, {QStringLiteral("accepted"), accept}});
        if (accept) {
            if (!userAvailable(me) || !userAvailable(from)) { error(socket, QStringLiteral("Một người chơi đã bận/thoát.")); return; }
            createMatch(from, me);
        } else notice(me, QStringLiteral("Đã từ chối lời mời từ %1.").arg(from));
        return;
    }
    if (type == QStringLiteral("move")) {
        const QJsonValue rv = msg.value(QStringLiteral("room"));
        const QJsonValue rr = msg.value(QStringLiteral("row"));
        const QJsonValue cc = msg.value(QStringLiteral("col"));
        if (!rv.isDouble() || !rr.isDouble() || !cc.isDouble()) { error(socket, QStringLiteral("Nước đi không hợp lệ.")); return; }
        int id = rv.toInt(-1), row = rr.toInt(-1), col = cc.toInt(-1);
        if (!rooms_.contains(id) || sessions_[me].roomId != id || sessions_[me].spectator) {
            error(socket, QStringLiteral("Không có quyền đánh trong phòng này.")); return;
        }
        Room &room = rooms_[id];
        if (!room.active || room.paused) { error(socket, QStringLiteral("Trận đang kết thúc hoặc tạm dừng.")); return; }
        if (room.deadline <= nowMs()) { error(socket, QStringLiteral("Đã hết thời gian lượt.")); return; }
        const QString activeName = room.turn == 1 ? room.x : room.o;
        if (me != activeName) { error(socket, QStringLiteral("Chưa đến lượt bạn.")); return; }
        if (row < 0 || row >= kSize || col < 0 || col >= kSize || room.board[row*kSize+col] != 0) {
            error(socket, QStringLiteral("Ô đánh không hợp lệ hoặc đã có quân.")); return;
        }
        room.board[row*kSize+col] = room.turn;
        room.lastRow = row; room.lastCol = col;
        room.moves.append(QJsonObject{{QStringLiteral("user"), me}, {QStringLiteral("row"), row},
            {QStringLiteral("col"), col}, {QStringLiteral("at"), timestamp(nowMs())}});
        log(QStringLiteral("MOVE #%1 user=%2 row=%3 col=%4").arg(id).arg(me).arg(row).arg(col));
        if (hasWinner(room, row, col, room.turn)) { finish(room, me, QStringLiteral("5 quân liên tiếp")); return; }
        if (!room.board.contains(0)) { finish(room, QStringLiteral("Hòa"), QStringLiteral("Bàn cờ đầy")); return; }
        room.turn = 3 - room.turn;
        room.deadline = nowMs() + kTurnSeconds * 1000;
        room.lastBroadcastSecond = -1;
        broadcastSnapshot(room);
        return;
    }
    if (type == QStringLiteral("spectate")) {
        if (!msg.value(QStringLiteral("room")).isDouble()) { error(socket, QStringLiteral("Phòng không hợp lệ.")); return; }
        int id = msg.value(QStringLiteral("room")).toInt(-1);
        if (!rooms_.contains(id) || !rooms_[id].active || sessions_[me].roomId >= 0) {
            error(socket, QStringLiteral("Không thể xem phòng này (đã bận hoặc phòng đã đóng).")); return;
        }
        Room &room = rooms_[id];
        room.spectators.append(me);
        sessions_[me].roomId = id;
        sessions_[me].spectator = true;
        broadcastSnapshot(room);
        broadcastUsers(); broadcastRooms();
        log(QStringLiteral("SPECTATE #%1 user=%2").arg(id).arg(me));
        return;
    }
    if (type == QStringLiteral("leave_spectate")) {
        Session &session = sessions_[me];
        if (!session.spectator || !rooms_.contains(session.roomId)) {
            error(socket, QStringLiteral("Bạn không phải khán giả.")); return;
        }
        Room &room = rooms_[session.roomId];
        room.spectators.removeAll(me);
        session.spectator = false; session.roomId = -1;
        send(socket, QJsonObject{{QStringLiteral("type"), QStringLiteral("left_room")}});
        broadcastSnapshot(room);
        broadcastUsers(); broadcastRooms();
        return;
    }
    error(socket, QStringLiteral("Loại yêu cầu không được hỗ trợ."));
}
void CaroServer::clockTick() {
    const qint64 now = nowMs();
    for (auto it = invites_.begin(); it != invites_.end();) {
        if (it->expiresAt < now || !userAvailable(it->from) || !userAvailable(it->to)) {
            if (it->expiresAt < now) notice(it->from, QStringLiteral("Lời mời tới %1 đã hết hạn.").arg(it->to));
            it = invites_.erase(it);
        } else ++it;
    }
    QList<int> ids = rooms_.keys();
    for (int id : ids) {
        if (!rooms_.contains(id)) continue;
        Room &room = rooms_[id];
        if (!room.active) {
            if (now - room.endedAt > 5 * 60000) rooms_.remove(id);
            continue;
        }
        // Grace expires independently of the per-turn clock.
        const Session &sx = sessions_[room.x], &so = sessions_[room.o];
        if (!sx.socket && sx.offlineSince > 0 && now - sx.offlineSince >= kGraceMs) {
            finish(room, room.o, QStringLiteral("X mất kết nối quá 90 giây")); continue;
        }
        if (!so.socket && so.offlineSince > 0 && now - so.offlineSince >= kGraceMs) {
            finish(room, room.x, QStringLiteral("O mất kết nối quá 90 giây")); continue;
        }
        if (room.paused) continue;
        if (now >= room.deadline) {
            finish(room, room.turn == 1 ? room.o : room.x, QStringLiteral("Hết 30 giây lượt"));
            continue;
        }
        const int secs = static_cast<int>((room.deadline - now + 999)/1000);
        if (room.lastBroadcastSecond != secs) {
            room.lastBroadcastSecond = secs;
            broadcastSnapshot(room);
        }
    }
    QStringList removable;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (!it->socket && it->roomId < 0 && it->offlineSince > 0 && now - it->offlineSince >= kGraceMs)
            removable.append(it.key());
    }
    for (const QString &name : removable) sessions_.remove(name);
}
