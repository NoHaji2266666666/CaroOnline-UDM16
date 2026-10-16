#include "MainWindow.h"
#include "BoardWidget.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QDateTime>
#include <QFont>
#include <QColor>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    buildUi();
    connect(&socket_, &QTcpSocket::connected, this, [this] {
        writeLog(QStringLiteral("Đã mở kết nối TCP, đang đăng nhập..."));
        sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("hello")},
            {QStringLiteral("name"), myName_}, {QStringLiteral("token"), token_}});
        refreshConnectionUi();
    });
    connect(&socket_, &QTcpSocket::readyRead, this, &MainWindow::received);
    connect(&socket_, &QTcpSocket::disconnected, this, [this] {
        joined_ = false;
        active_ = false; // UI chỉ cho đánh sau khi nhận snapshot mới
        writeLog(QStringLiteral("Mất kết nối với Server."));
        refreshConnectionUi();
    });
    connect(&socket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        writeLog(QStringLiteral("TCP: ") + socket_.errorString());
    });
    connect(board_, &BoardWidget::cellClicked, this, &MainWindow::playAt);
    auto *reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(2000);
    connect(reconnectTimer, &QTimer::timeout, this, [this] {
        if (wantConnection_ && socket_.state() == QAbstractSocket::UnconnectedState) startConnection();
    });
    reconnectTimer->start();
    refreshConnectionUi();
}
void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("Caro Online | UDM_16 | Qt Desktop"));
    resize(1320, 810);
    auto *root = new QWidget(this);
    setCentralWidget(root);
    auto *column = new QVBoxLayout(root);
    column->setSpacing(10);

    auto *top = new QHBoxLayout();
    hostInput_ = new QLineEdit(QStringLiteral("127.0.0.1"));
    hostInput_->setMaximumWidth(180);
    portInput_ = new QSpinBox();
    portInput_->setRange(1, 65535);
    portInput_->setValue(45454);
    nameInput_ = new QLineEdit();
    nameInput_->setPlaceholderText(QStringLiteral("VD: alice (3-20 ký tự)"));
    nameInput_->setMaximumWidth(190);
    connectionButton_ = new QPushButton(QStringLiteral("Kết nối"));
    connectionLabel_ = new QLabel(QStringLiteral("Chưa kết nối"));
    top->addWidget(new QLabel(QStringLiteral("Server IP:")));
    top->addWidget(hostInput_);
    top->addWidget(new QLabel(QStringLiteral("Port:")));
    top->addWidget(portInput_);
    top->addWidget(new QLabel(QStringLiteral("Tên:")));
    top->addWidget(nameInput_);
    top->addWidget(connectionButton_);
    top->addWidget(connectionLabel_, 1);
    column->addLayout(top);
    connect(connectionButton_, &QPushButton::clicked, this, &MainWindow::startOrStop);

    auto *body = new QHBoxLayout();
    column->addLayout(body, 1);

    auto *left = new QVBoxLayout();
    left->setSpacing(6);
    auto *onlineGroup = new QGroupBox(QStringLiteral("Người chơi đang online"));
    auto *onlineLayout = new QVBoxLayout(onlineGroup);
    usersList_ = new QListWidget();
    onlineLayout->addWidget(usersList_);
    auto *inviteButton = new QPushButton(QStringLiteral("Mời người được chọn"));
    onlineLayout->addWidget(inviteButton);
    connect(inviteButton, &QPushButton::clicked, this, &MainWindow::inviteSelected);
    left->addWidget(onlineGroup, 1);

    auto *roomsGroup = new QGroupBox(QStringLiteral("Phòng đang đấu"));
    auto *roomsLayout = new QVBoxLayout(roomsGroup);
    roomsList_ = new QListWidget();
    roomsLayout->addWidget(roomsList_);
    auto *watchButton = new QPushButton(QStringLiteral("Vào xem phòng đã chọn"));
    roomsLayout->addWidget(watchButton);
    connect(watchButton, &QPushButton::clicked, this, &MainWindow::spectateSelected);
    leaveButton_ = new QPushButton(QStringLiteral("Rời ghế khán giả"));
    roomsLayout->addWidget(leaveButton_);
    connect(leaveButton_, &QPushButton::clicked, this, &MainWindow::leaveSpectate);
    left->addWidget(roomsGroup, 1);
    auto *refreshButton = new QPushButton(QStringLiteral("Làm mới danh sách"));
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshLists);
    left->addWidget(refreshButton);
    body->addLayout(left, 1);

    auto *center = new QVBoxLayout();
    gameLabel_ = new QLabel(QStringLiteral("Chưa có trận. Mời người chơi để bắt đầu."));
    QFont titleFont = gameLabel_->font(); titleFont.setPointSize(titleFont.pointSize()+2); titleFont.setBold(true);
    gameLabel_->setFont(titleFont);
    gameLabel_->setWordWrap(true);
    center->addWidget(gameLabel_);
    clockLabel_ = new QLabel(QStringLiteral("Đồng hồ: --"));
    clockLabel_->setStyleSheet(QStringLiteral("QLabel{font-size:18px;color:#254c87;font-weight:600;}"));
    center->addWidget(clockLabel_);
    board_ = new BoardWidget();
    center->addWidget(board_, 1);
    body->addLayout(center, 3);

    auto *right = new QVBoxLayout();
    auto *historyGroup = new QGroupBox(QStringLiteral("Lịch sử trận (tối đa 50 trận gần nhất)"));
    auto *historyLayout = new QVBoxLayout(historyGroup);
    historyTable_ = new QTableWidget(0, 5);
    historyTable_->setHorizontalHeaderLabels(QStringList{QStringLiteral("Phòng"), QStringLiteral("X vs O"),
                QStringLiteral("Thắng"), QStringLiteral("Lý do"), QStringLiteral("Kết thúc UTC")});
    historyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    historyTable_->horizontalHeader()->setStretchLastSection(true);
    historyTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    historyLayout->addWidget(historyTable_);
    auto *historyButton = new QPushButton(QStringLiteral("Tải lại lịch sử"));
    connect(historyButton, &QPushButton::clicked, this, [this] {
        sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("history")}});
    });
    historyLayout->addWidget(historyButton);
    right->addWidget(historyGroup, 3);
    auto *logGroup = new QGroupBox(QStringLiteral("Nhật ký Client"));
    auto *logLayout = new QVBoxLayout(logGroup);
    logText_ = new QTextEdit();
    logText_->setReadOnly(true);
    logLayout->addWidget(logText_);
    right->addWidget(logGroup, 2);
    body->addLayout(right, 2);
    statusBar()->showMessage(QStringLiteral("Bước 1: chạy Server; bước 2: mở ít nhất hai CaroClient với tên khác nhau."));
}
void MainWindow::writeLog(const QString &message) {
    logText_->append(QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), message.toHtmlEscaped()));
}
QString MainWindow::settingsTokenKey() const {
    return QStringLiteral("tokens/") + nameInput_->text().trimmed();
}
void MainWindow::startOrStop() {
    if (wantConnection_) {
        wantConnection_ = false;
        socket_.abort();
        joined_ = active_ = spectator_ = false;
        roomId_ = -1;
        clockLabel_->setText(QStringLiteral("Đồng hồ: --"));
        refreshConnectionUi();
        return;
    }
    const QString name = nameInput_->text().trimmed();
    if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_]{3,20}$")).match(name).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("Tên không hợp lệ"),
                             QStringLiteral("Tên cần 3–20 ký tự a-z, A-Z, 0-9 hoặc _."));
        return;
    }
    myName_ = name;
    token_ = settings_.value(settingsTokenKey()).toString();
    wantConnection_ = true;
    hostInput_->setEnabled(false); portInput_->setEnabled(false); nameInput_->setEnabled(false);
    startConnection();
    refreshConnectionUi();
}
void MainWindow::startConnection() {
    if (!wantConnection_) return;
    incoming_.clear();
    socket_.connectToHost(hostInput_->text().trimmed(), static_cast<quint16>(portInput_->value()));
    writeLog(QStringLiteral("Đang kết nối %1:%2...").arg(hostInput_->text()).arg(portInput_->value()));
    refreshConnectionUi();
}
void MainWindow::refreshConnectionUi() {
    const bool ok = socket_.state() == QAbstractSocket::ConnectedState && joined_;
    connectionLabel_->setText(ok ? QStringLiteral("Đã đăng nhập: ") + myName_
                             : (wantConnection_ ? QStringLiteral("Đang kết nối / chờ reconnect...")
                                                : QStringLiteral("Chưa kết nối")));
    connectionButton_->setText(wantConnection_ ? QStringLiteral("Ngắt kết nối") : QStringLiteral("Kết nối"));
    hostInput_->setEnabled(!wantConnection_);
    portInput_->setEnabled(!wantConnection_);
    nameInput_->setEnabled(!wantConnection_);
    leaveButton_->setEnabled(ok && spectator_ && active_);
}
void MainWindow::sendMessage(const QJsonObject &msg) {
    if (socket_.state() != QAbstractSocket::ConnectedState) {
        writeLog(QStringLiteral("Chưa kết nối đến Server."));
        return;
    }
    QByteArray bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    bytes.append('\n');
    socket_.write(bytes);
}
void MainWindow::received() {
    incoming_.append(socket_.readAll());
    if (incoming_.size() > 131072) { socket_.abort(); writeLog(QStringLiteral("Gói Server quá lớn.")); return; }
    while (true) {
        const int end = incoming_.indexOf('\n');
        if (end == -1) break;
        const QByteArray frame = incoming_.left(end);
        incoming_.remove(0, end+1);
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(frame, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            writeLog(QStringLiteral("Dữ liệu Server không hợp lệ.")); continue;
        }
        eventMessage(doc.object());
    }
}
void MainWindow::eventMessage(const QJsonObject &msg) {
    const QString type = msg.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("welcome")) {
        joined_ = true;
        token_ = msg.value(QStringLiteral("token")).toString();
        // Token là phiên demo để reconnect; không có chức năng mật khẩu/đăng ký.
        settings_.setValue(settingsTokenKey(), token_);
        writeLog(msg.value(QStringLiteral("resumed")).toBool() ? QStringLiteral("Đã phục hồi phiên cũ.")
                                                                  : QStringLiteral("Đăng nhập thành công."));
        refreshConnectionUi();
        refreshLists();
        return;
    }
    if (type == QStringLiteral("error") || type == QStringLiteral("notice")) {
        writeLog((type == QStringLiteral("error") ? QStringLiteral("Lỗi: ") : QStringLiteral("Thông báo: "))
                 + msg.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QStringLiteral("users")) {
        const QString previous = usersList_->currentItem() ? usersList_->currentItem()->data(Qt::UserRole).toString() : QString();
        usersList_->clear();
        for (const auto &value : msg.value(QStringLiteral("users")).toArray()) {
            const QJsonObject user = value.toObject();
            QString name = user.value(QStringLiteral("name")).toString();
            const bool busy = user.value(QStringLiteral("busy")).toBool();
            auto *item = new QListWidgetItem(name + (busy ? QStringLiteral(" (đang bận)") : QStringLiteral(" (rảnh)")));
            item->setData(Qt::UserRole, name);
            if (name == myName_) item->setForeground(QColor(99, 112, 135));
            usersList_->addItem(item);
            if (name == previous) usersList_->setCurrentItem(item);
        }
        return;
    }
    if (type == QStringLiteral("invite_received")) {
        const QString from = msg.value(QStringLiteral("from")).toString();
        writeLog(QStringLiteral("Có lời mời từ %1.").arg(from));
        const bool accepted = QMessageBox::question(this, QStringLiteral("Lời mời thách đấu"),
            QStringLiteral("%1 mời bạn chơi Caro. Đồng ý?").arg(from),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
        sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("invite_reply")},
                                {QStringLiteral("from"), from}, {QStringLiteral("accept"), accepted}});
        return;
    }
    if (type == QStringLiteral("invite_result")) {
        writeLog(QStringLiteral("%1 đã %2 lời mời.").arg(msg.value(QStringLiteral("target")).toString(),
                msg.value(QStringLiteral("accepted")).toBool() ? QStringLiteral("chấp nhận") : QStringLiteral("từ chối")));
        return;
    }
    if (type == QStringLiteral("rooms")) {
        const int previous = roomsList_->currentItem() ? roomsList_->currentItem()->data(Qt::UserRole).toInt() : -1;
        roomsList_->clear();
        for (const auto &value : msg.value(QStringLiteral("rooms")).toArray()) {
            const QJsonObject item = value.toObject();
            const int id = item.value(QStringLiteral("id")).toInt();
            auto *line = new QListWidgetItem(QStringLiteral("#%1  %2 vs %3 | Xem: %4%5")
                 .arg(id).arg(item.value(QStringLiteral("x")).toString(), item.value(QStringLiteral("o")).toString())
                 .arg(item.value(QStringLiteral("spectators")).toInt())
                 .arg(item.value(QStringLiteral("paused")).toBool() ? QStringLiteral(" | Tạm dừng") : QString()));
            line->setData(Qt::UserRole, id);
            roomsList_->addItem(line);
            if (id == previous) roomsList_->setCurrentItem(line);
        }
        return;
    }
    if (type == QStringLiteral("snapshot")) { applySnapshot(msg); return; }
    if (type == QStringLiteral("left_room")) {
        roomId_ = -1; active_ = spectator_ = false;
        board_->resetBoard();
        gameLabel_->setText(QStringLiteral("Đã rời phòng khán giả."));
        clockLabel_->setText(QStringLiteral("Đồng hồ: --"));
        refreshConnectionUi(); return;
    }
    if (type == QStringLiteral("history_result")) {
        const QJsonArray matches = msg.value(QStringLiteral("matches")).toArray();
        historyTable_->setRowCount(matches.size());
        for (int row = 0; row < matches.size(); ++row) {
            const QJsonObject match = matches.at(row).toObject();
            const QStringList columns{
                QString::number(match.value(QStringLiteral("room")).toInt()),
                match.value(QStringLiteral("x")).toString() + QStringLiteral(" vs ") + match.value(QStringLiteral("o")).toString(),
                match.value(QStringLiteral("winner")).toString(),
                match.value(QStringLiteral("reason")).toString(),
                match.value(QStringLiteral("endedAt")).toString()};
            for (int col = 0; col < columns.size(); ++col)
                historyTable_->setItem(row, col, new QTableWidgetItem(columns.at(col)));
        }
        historyTable_->resizeColumnsToContents();
        return;
    }
    writeLog(QStringLiteral("Sự kiện chưa biết: ") + type);
}
void MainWindow::applySnapshot(const QJsonObject &msg) {
    const int id = msg.value(QStringLiteral("room")).toInt(-1);
    if (id < 0) return;
    roomId_ = id;
    active_ = msg.value(QStringLiteral("active")).toBool();
    const QString x = msg.value(QStringLiteral("x")).toString();
    const QString o = msg.value(QStringLiteral("o")).toString();
    spectator_ = myName_ != x && myName_ != o;
    currentTurn_ = msg.value(QStringLiteral("turn")).toString();
    QStringList board;
    for (const QJsonValue &row : msg.value(QStringLiteral("board")).toArray()) board << row.toString();
    board_->setBoard(board, msg.value(QStringLiteral("lastRow")).toInt(-1), msg.value(QStringLiteral("lastCol")).toInt(-1));
    const QString role = spectator_ ? QStringLiteral("Khán giả") : (myName_ == x ? QStringLiteral("Bạn là X") : QStringLiteral("Bạn là O"));
    if (active_) {
        const QString turnName = currentTurn_ == QStringLiteral("X") ? x : o;
        const QString pause = msg.value(QStringLiteral("paused")).toBool() ? QStringLiteral(" | TẠM DỪNG (chờ reconnect)") : QString();
        gameLabel_->setText(QStringLiteral("Phòng #%1 | X: %2%3 | O: %4%5 | %6")
              .arg(id).arg(x, msg.value(QStringLiteral("onlineX")).toBool() ? QString() : QStringLiteral(" (offline)"),
                           o, msg.value(QStringLiteral("onlineO")).toBool() ? QString() : QStringLiteral(" (offline)"), role));
        clockLabel_->setText(QStringLiteral("Lượt: %1 (%2) | còn %3 giây%4 | %5 người xem")
            .arg(turnName, currentTurn_).arg(msg.value(QStringLiteral("secondsLeft")).toInt()).arg(pause)
            .arg(msg.value(QStringLiteral("spectators")).toInt()));
    } else {
        gameLabel_->setText(QStringLiteral("Phòng #%1 kết thúc | X: %2 | O: %3 | %4").arg(id).arg(x, o, role));
        clockLabel_->setText(QStringLiteral("Kết quả: %1 | %2")
                   .arg(msg.value(QStringLiteral("winner")).toString(), msg.value(QStringLiteral("reason")).toString()));
    }
    refreshConnectionUi();
}
void MainWindow::refreshLists() {
    if (!joined_) return;
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("list_users")}});
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("list_rooms")}});
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("history")}});
}
void MainWindow::inviteSelected() {
    if (!joined_) return;
    auto *item = usersList_->currentItem();
    if (!item) { QMessageBox::information(this, QStringLiteral("Chọn người chơi"), QStringLiteral("Hãy chọn tên trong danh sách.")); return; }
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("invite")},
        {QStringLiteral("target"), item->data(Qt::UserRole).toString()}});
}
void MainWindow::spectateSelected() {
    if (!joined_) return;
    auto *item = roomsList_->currentItem();
    if (!item) { QMessageBox::information(this, QStringLiteral("Chọn phòng"), QStringLiteral("Hãy chọn phòng đang diễn ra.")); return; }
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("spectate")},
        {QStringLiteral("room"), item->data(Qt::UserRole).toInt()}});
}
void MainWindow::playAt(int row, int col) {
    if (!joined_ || !active_ || spectator_ || roomId_ < 0) return;
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("move")},
                            {QStringLiteral("room"), roomId_}, {QStringLiteral("row"), row}, {QStringLiteral("col"), col}});
}
void MainWindow::leaveSpectate() {
    if (!joined_ || !spectator_ || !active_) return;
    sendMessage(QJsonObject{{QStringLiteral("type"), QStringLiteral("leave_spectate")}});
}
