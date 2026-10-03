#include "ui/ChatWindow.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Models.h"
#include "net/Prefs.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTime>
#include <QVBoxLayout>

ChatWindow::ChatWindow(ApiClient* api, WsClient* ws, const QString& peerId,
                       const QString& name, const QString& avatarUrl, QWidget* mainWin)
    : QWidget(mainWin, Qt::Window), api_(api), ws_(ws),
      peerId_(peerId), name_(name), avatarUrl_(avatarUrl) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(QStringLiteral("%1 — Xipher").arg(name));
    resize(480, 620);
    setStyleSheet(QStringLiteral(
        "ChatWindow{background:#131218;}"
        "QLabel{color:#F3F1F8;}"
        "#cwHeader{background:#131218;border-bottom:1px solid rgba(255,255,255,0.10);}"
        "#cwName{font-size:16px;font-weight:700;color:#F3F1F8;}"
        "#cwStatus{font-size:12px;color:#726C82;}"
        "QPlainTextEdit{background:#1A1822;border:1px solid rgba(255,255,255,0.10);"
        "border-radius:14px;color:#F3F1F8;font-size:14px;padding:8px 12px;"
        "selection-background-color:#8B5CF6;}"
        "#cwSend{background:#8B5CF6;border:none;border-radius:16px;color:#fff;"
        "font-size:14px;font-weight:700;min-width:72px;min-height:34px;}"
        "#cwSend:hover{background:#9B72F8;}"
        "#cwBubble{background:#221F2C;border-radius:14px;color:#F3F1F8;"
        "font-size:14px;padding:8px 12px;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Шапка: имя/статус.
    auto* head = new QWidget(this);
    head->setObjectName(QStringLiteral("cwHeader"));
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(14, 10, 14, 10);
    auto* idCol = new QVBoxLayout();
    idCol->setSpacing(1);
    nameLbl_ = new QLabel(name, head);
    nameLbl_->setObjectName(QStringLiteral("cwName"));
    statusLbl_ = new QLabel(QStringLiteral("отдельное окно"), head);
    statusLbl_->setObjectName(QStringLiteral("cwStatus"));
    idCol->addWidget(nameLbl_);
    idCol->addWidget(statusLbl_);
    hl->addLayout(idCol);
    hl->addStretch();
    root->addWidget(head);

    // История: скролл со «страницами»-бабблами.
    auto* sa = new QScrollArea(this);
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setStyleSheet(QStringLiteral("QScrollArea{background:#0B0A0E;}"));
    historyHost_ = new QWidget();
    historyHost_->setStyleSheet(QStringLiteral("background:#0B0A0E;"));
    historyLay_ = new QVBoxLayout(historyHost_);
    historyLay_->setContentsMargins(12, 12, 12, 12);
    historyLay_->setSpacing(6);
    historyLay_->addStretch();
    sa->setWidget(historyHost_);
    root->addWidget(sa, 1);

    // Композер: поле + Enter-отправка.
    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 8, 10, 10);
    bl->setSpacing(8);
    composer_ = new QPlainTextEdit(bar);
    composer_->setFixedHeight(52);
    composer_->setPlaceholderText(QStringLiteral("Сообщение… (Enter — отправить)"));
    sendBtn_ = new QPushButton(QStringLiteral("➤"), bar);
    sendBtn_->setObjectName(QStringLiteral("cwSend"));
    sendBtn_->setCursor(Qt::PointingHandCursor);
    bl->addWidget(composer_, 1);
    bl->addWidget(sendBtn_, 0, Qt::AlignBottom);
    root->addWidget(bar);

    connect(sendBtn_, &QPushButton::clicked, this, &ChatWindow::sendCurrent);
    // Свои и чужие сообщения этого peerId приходят общим эхом.
    connect(ws_, &WsClient::newMessage, this,
            [this](const QString& pid, const ChatMessage& m, const QString&) {
        if (pid != peerId_ || m.sent) return;
        appendMessage(m.content.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")),
                      false);
    });
    connect(api_, &ApiClient::messageSent, this,
            [this](const ChatMessage& m, const QString& receiverId, const QString&) {
        if (receiverId != peerId_ || !m.sent || m.id.startsWith(QStringLiteral("tmp"))) return;
        // Эхо своей отправки (если ещё не нарисовали оптимистично).
    });

    remember(peerId_, name);
}

void ChatWindow::appendMessage(const QString& html, bool own) {
    auto* b = new QLabel(historyHost_);
    b->setObjectName(QStringLiteral("cwBubble"));
    b->setTextFormat(Qt::RichText);
    b->setText(html);
    b->setWordWrap(true);
    b->setMaximumWidth(360);
    historyLay_->insertWidget(historyLay_->count() - 1, b, 0,
                              own ? Qt::AlignRight : Qt::AlignLeft);
}

void ChatWindow::sendCurrent() {
    const QString text = composer_->toPlainText().trimmed();
    if (text.isEmpty()) return;
    const QString tempId = QStringLiteral("cwwin_%1_%2").arg(peerId_).arg(++counter_);
    api_->sendMessage(peerId_, text, tempId);
    appendMessage(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")), true);
    composer_->clear();
}

void ChatWindow::closeEvent(QCloseEvent* e) {
    forget(peerId_);
    emit closed(peerId_);
    QWidget::closeEvent(e);
}

// ── Список открытых окон (QSettings, WIN-04) ────────────────────────────────

static const char* kWinKey = "xipher_detached_windows";

QStringList ChatWindow::saveList() {
    return Prefs::getStr(QLatin1String(kWinKey))
        .split(QLatin1Char(';'), Qt::SkipEmptyParts);
}

void ChatWindow::remember(const QString& peerId, const QString& name) {
    QStringList ids = saveList();
    const QString entry = peerId;
    if (!ids.contains(entry)) ids << entry;
    Prefs::setStr(QLatin1String(kWinKey), ids.join(QLatin1Char(';')));
    Prefs::store().setValue(QStringLiteral("xipher_detached_name_%1").arg(peerId), name);
}

void ChatWindow::forget(const QString& peerId) {
    QStringList ids = saveList();
    ids.removeAll(peerId);
    Prefs::setStr(QLatin1String(kWinKey), ids.join(QLatin1Char(';')));
}

void ChatWindow::clearAll() {
    Prefs::setStr(QLatin1String(kWinKey), QString());
}
