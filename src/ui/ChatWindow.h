#pragma once
#include <QWidget>
#include <QJsonArray>

class ApiClient;
class WsClient;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;

// ─────────────────────────────────────────────────────────────────────────────
//  ChatWindow — отдельное окно одного чата (WIN-04, drag-out как в ТГ):
//  шапка (имя/статус), история, композер Enter-отправки. Отдельное окно
//  подписывается на WS-новости своего peerId; список открытых окон живёт
//  в QSettings и восстанавливается после релогина.
// ─────────────────────────────────────────────────────────────────────────────
class ChatWindow : public QWidget {
    Q_OBJECT
public:
    ChatWindow(ApiClient* api, WsClient* ws, const QString& peerId,
               const QString& name, const QString& avatarUrl, QWidget* mainWin);

    QString peerId() const { return peerId_; }
    // Список открытых окон (WIN-04: восстановление после релогина).
    static QStringList saveList();
    static void remember(const QString& peerId, const QString& name);
    static void forget(const QString& peerId);
    static void clearAll();

signals:
    void closed(const QString& peerId);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void appendMessage(const QString& html, bool own);
    void sendCurrent();

    ApiClient* api_;
    WsClient*  ws_;
    QString peerId_, name_, avatarUrl_;
    QLabel* nameLbl_ = nullptr;
    QLabel* statusLbl_ = nullptr;
    QVBoxLayout* historyLay_ = nullptr;
    QWidget* historyHost_ = nullptr;
    QPlainTextEdit* composer_ = nullptr;
    QPushButton* sendBtn_ = nullptr;
    int counter_ = 0;
};
