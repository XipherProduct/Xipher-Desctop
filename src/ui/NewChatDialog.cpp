#include "ui/NewChatDialog.h"
#include "ui/AvatarUtil.h"
#include "net/ApiClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

namespace {

QLabel* avatar(const QString& text, int size) {
    auto* a = new QLabel();
    a->setFixedSize(size, size);
    a->setAlignment(Qt::AlignCenter);
    QString l = text.trimmed().left(1).toUpper();
    a->setText(l.isEmpty() ? QStringLiteral("?") : l);
    a->setStyleSheet(QString(
        "border-radius:%1px;color:#fff;font-weight:700;font-size:%2px;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);")
        .arg(size / 2).arg(size * 4 / 10));
    return a;
}

} // namespace

NewChatDialog::NewChatDialog(ApiClient* api, QWidget* parent)
    : QDialog(parent), api_(api) {
    setWindowTitle(QStringLiteral("Новый чат"));
    setModal(true);
    resize(480, 620);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(350);

    buildUi();

    connect(debounce_, &QTimer::timeout, this, &NewChatDialog::runSearch);
    connect(search_, &QLineEdit::textChanged, this, &NewChatDialog::onSearchChanged);
    connect(api_, &ApiClient::usersFound,          this, &NewChatDialog::onUsersFound);
    connect(api_, &ApiClient::friendRequestSent,   this, &NewChatDialog::onFriendRequestSent);
    connect(api_, &ApiClient::friendRequestsLoaded, this, &NewChatDialog::onRequestsLoaded);
    connect(api_, &ApiClient::friendActionDone,    this, &NewChatDialog::onFriendActionDone);

    api_->getFriendRequests();   // подгрузим входящие сразу
}

void NewChatDialog::buildUi() {
    setStyleSheet(QStringLiteral(R"QSS(
QDialog { background:#131218; }
QLabel { color:#F3F1F8; }
#dlgTitle { font-size:18px; font-weight:800; }
#reqTitle { font-size:13px; font-weight:700; color:#ACA6BD; }
#status { color:#726C82; font-size:12px; }
QLineEdit {
    background:#1A1822; border:1px solid rgba(255,255,255,10%); border-radius:12px;
    min-height:40px; padding:0 14px; color:#F3F1F8; font-size:14px;
}
QLineEdit:focus { border:1px solid #8B5CF6; }
QListWidget { background:#0F0E14; border:1px solid rgba(255,255,255,7%); border-radius:12px; outline:none; }
QListWidget::item { border:none; }
QListWidget::item:hover { background:#1A1822; }
QPushButton.act {
    border:none; border-radius:10px; padding:6px 12px; font-size:12px; font-weight:700; color:#fff;
    background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);
}
QPushButton.act:hover { background:#9B72F8; }
QPushButton.ghost {
    border:1px solid rgba(255,255,255,14%); border-radius:10px; padding:6px 12px;
    font-size:12px; font-weight:600; color:#ACA6BD; background:transparent;
}
QPushButton.ghost:hover { color:#F3F1F8; border-color:#8B5CF6; }
)QSS"));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(12);

    auto* title = new QLabel(QStringLiteral("Новый чат"), this);
    title->setObjectName(QStringLiteral("dlgTitle"));
    lay->addWidget(title);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("Поиск по имени или @username"));
    lay->addWidget(search_);

    status_ = new QLabel(QStringLiteral("Введите минимум 1 символ для поиска"), this);
    status_->setObjectName(QStringLiteral("status"));
    lay->addWidget(status_);

    results_ = new QListWidget(this);
    results_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    lay->addWidget(results_, 1);

    reqTitle_ = new QLabel(QStringLiteral("Заявки в друзья"), this);
    reqTitle_->setObjectName(QStringLiteral("reqTitle"));
    lay->addWidget(reqTitle_);

    requests_ = new QListWidget(this);
    requests_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    requests_->setMaximumHeight(180);
    lay->addWidget(requests_);
}

void NewChatDialog::onSearchChanged() {
    debounce_->start();
}

void NewChatDialog::runSearch() {
    const QString q = search_->text().trimmed();
    lastQuery_ = q;
    if (q.isEmpty()) {
        results_->clear();
        status_->setText(QStringLiteral("Введите минимум 1 символ для поиска"));
        return;
    }
    status_->setText(QStringLiteral("Поиск…"));
    api_->searchUsers(q);
}

void NewChatDialog::onUsersFound(const QString& query, const QList<UserHit>& users) {
    if (query != lastQuery_) return;   // устаревший ответ
    results_->clear();
    status_->setText(users.isEmpty() ? QStringLiteral("Никого не найдено")
                                     : QStringLiteral("Найдено: %1").arg(users.size()));

    for (const UserHit& u : users) {
        auto* row = new QWidget();
        row->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(10, 8, 10, 8);
        rl->setSpacing(10);
        auto* av = new QLabel();
        Avatar::setRound(av, u.avatarUrl, u.displayName.isEmpty() ? u.username : u.displayName, 40);
        rl->addWidget(av);

        auto* col = new QVBoxLayout();
        col->setSpacing(1);
        auto* name = new QLabel(u.displayName, row);
        name->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;color:#F3F1F8;"));
        auto* uname = new QLabel(QStringLiteral("@") + u.username, row);
        uname->setStyleSheet(QStringLiteral("font-size:12px;color:#726C82;"));
        col->addWidget(name);
        col->addWidget(uname);
        rl->addLayout(col, 1);

        auto* write = new QPushButton(QStringLiteral("Написать"), row);
        write->setProperty("class", "act");
        write->setCursor(Qt::PointingHandCursor);
        auto* addf = new QPushButton(QStringLiteral("＋ В друзья"), row);
        addf->setProperty("class", "ghost");
        addf->setCursor(Qt::PointingHandCursor);
        rl->addWidget(write);
        rl->addWidget(addf);

        const QString id = u.id, dn = u.displayName, un = u.username;
        connect(write, &QPushButton::clicked, this, [this, id, dn, un]() {
            emit openChatRequested(id, dn, un);
            accept();
        });
        connect(addf, &QPushButton::clicked, this, [this, un, addf]() {
            addf->setEnabled(false);
            addf->setText(QStringLiteral("…"));
            api_->sendFriendRequest(un);
        });

        auto* item = new QListWidgetItem(results_);
        item->setSizeHint(QSize(0, 58));
        results_->addItem(item);
        results_->setItemWidget(item, row);
    }
}

void NewChatDialog::onFriendRequestSent(const QString& username, bool ok, const QString& message) {
    status_->setText(ok ? QStringLiteral("Заявка отправлена: @%1").arg(username)
                        : QStringLiteral("Не удалось: %1").arg(message));
    // Кнопки конкретной строки уже задизейблены; повторный поиск обновит список.
}

void NewChatDialog::onRequestsLoaded(const QList<FriendRequest>& requests) {
    requests_->clear();
    reqTitle_->setText(requests.isEmpty()
        ? QStringLiteral("Заявки в друзья — нет новых")
        : QStringLiteral("Заявки в друзья (%1)").arg(requests.size()));

    for (const FriendRequest& r : requests) {
        auto* row = new QWidget();
        row->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(10, 6, 10, 6);
        rl->setSpacing(10);
        rl->addWidget(avatar(r.senderUsername, 36));

        auto* name = new QLabel(QStringLiteral("@") + r.senderUsername, row);
        name->setStyleSheet(QStringLiteral("font-size:13px;font-weight:600;color:#F3F1F8;"));
        rl->addWidget(name, 1);

        auto* acc = new QPushButton(QStringLiteral("Принять"), row);
        acc->setProperty("class", "act");
        acc->setCursor(Qt::PointingHandCursor);
        auto* rej = new QPushButton(QStringLiteral("✕"), row);
        rej->setProperty("class", "ghost");
        rej->setCursor(Qt::PointingHandCursor);
        rl->addWidget(acc);
        rl->addWidget(rej);

        const QString rid = r.id;
        connect(acc, &QPushButton::clicked, this, [this, rid]() { api_->acceptFriend(rid); });
        connect(rej, &QPushButton::clicked, this, [this, rid]() { api_->rejectFriend(rid); });

        auto* item = new QListWidgetItem(requests_);
        item->setSizeHint(QSize(0, 50));
        requests_->addItem(item);
        requests_->setItemWidget(item, row);
    }
}

void NewChatDialog::onFriendActionDone(const QString& requestId, bool accepted, bool ok) {
    Q_UNUSED(requestId);
    if (ok && accepted) emit friendsChanged();   // обновим список чатов
    api_->getFriendRequests();                     // перерисуем заявки
}
