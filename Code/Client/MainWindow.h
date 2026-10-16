#pragma once
#include <QMainWindow>
#include <QTcpSocket>
#include <QJsonObject>
#include <QSettings>

class QLineEdit;
class QSpinBox;
class QLabel;
class QPushButton;
class QListWidget;
class QTableWidget;
class QTextEdit;
class BoardWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QTcpSocket socket_;
    QByteArray incoming_;
    QSettings settings_{QStringLiteral("LTMang"), QStringLiteral("CaroOnline")};
    QString myName_;
    QString token_;
    bool wantConnection_ = false;
    bool joined_ = false;
    bool active_ = false;
    bool spectator_ = false;
    QString currentTurn_;
    int roomId_ = -1;

    QLineEdit *hostInput_ = nullptr, *nameInput_ = nullptr;
    QSpinBox *portInput_ = nullptr;
    QLabel *connectionLabel_ = nullptr, *gameLabel_ = nullptr, *clockLabel_ = nullptr;
    QPushButton *connectionButton_ = nullptr, *leaveButton_ = nullptr;
    QListWidget *usersList_ = nullptr, *roomsList_ = nullptr;
    QTableWidget *historyTable_ = nullptr;
    QTextEdit *logText_ = nullptr;
    BoardWidget *board_ = nullptr;

    void buildUi();
    void startOrStop();
    void startConnection();
    void sendMessage(const QJsonObject &msg);
    void received();
    void eventMessage(const QJsonObject &msg);
    void applySnapshot(const QJsonObject &msg);
    void writeLog(const QString &message);
    void refreshConnectionUi();
    void refreshLists();
    void inviteSelected();
    void spectateSelected();
    void playAt(int row, int col);
    void leaveSpectate();
    QString settingsTokenKey() const;
};
