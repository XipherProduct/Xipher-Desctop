#include "ui/ContactsPanel.h"
#include "ui/AvatarUtil.h"
#include "net/ApiClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QTimer>
#include <QEvent>
#include <QMenu>
#include <QContextMenuEvent>

ContactsPanel::ContactsPanel(ApiClient* api, QWidget* parent)
    : ModalOverlay(parent, 460), api_(api) {
    card()->setFixedHeight(580);
    card()->setStyleSheet(QStringLiteral(R"QSS(
#modalCard{background:#17151E;border:1px solid rgba(255,255,255,8%);border-radius:18px;}
QLabel{color:#F3F1F8;}
#dlgHeader{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #6D28D9,stop:0.55 #8B5CF6,stop:1 #B06CF0);
  border-top-left-radius:18px;border-top-right-radius:18px;}
#dlgTitle{font-size:17px;font-weight:800;color:#fff;}
#closeBtn{background:rgba(255,255,255,16%);border:none;border-radius:16px;color:#fff;font-size:16px;font-weight:700;}
#closeBtn:hover{background:rgba(255,255,255,30%);}
QLineEdit{background:#131218;border:1px solid rgba(255,255,255,10%);border-radius:12px;
  min-height:38px;padding:0 12px;color:#F3F1F8;selection-background-color:#8B5CF6;}
QLineEdit:focus{border:1px solid #8B5CF6;}
#secLabel{color:#8B5CF6;font-size:12px;font-weight:800;letter-spacing:1px;}
#personRow{background:transparent;border-radius:10px;}
#personRow:hover{background:#1A1822;}
#name{color:#F3F1F8;font-size:14px;font-weight:600;}
#sub{color:#726C82;font-size:12px;}
#online{color:#46B98A;font-size:12px;}
#addBtn{background:rgba(139,92,246,20%);color:#C9B6FF;border:none;border-radius:9px;min-height:30px;padding:0 14px;font-weight:600;}
#addBtn:hover{background:rgba(139,92,246,34%);}
#okBtn{background:rgba(70,185,138,18%);color:#7FE0B6;border:none;border-radius:8px;min-width:32px;min-height:30px;font-weight:700;}
#okBtn:hover{background:rgba(70,185,138,30%);}
#noBtn{background:rgba(229,104,122,16%);color:#F0909C;border:none;border-radius:8px;min-width:32px;min-height:30px;font-weight:700;}
#noBtn:hover{background:rgba(229,104,122,28%);}
#empty{color:#726C82;font-size:13px;}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:rgba(255,255,255,12%);border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
)QSS"));

    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* head = new QWidget();
    head->setObjectName(QStringLiteral("dlgHeader"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setFixedHeight(52);
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(20, 0, 12, 0);
    auto* title = new QLabel(QStringLiteral("Контакты"));
    title->setObjectName(QStringLiteral("dlgTitle"));
    auto* close = new QPushButton(QStringLiteral("✕"));
    close->setObjectName(QStringLiteral("closeBtn"));
    close->setCursor(Qt::PointingHandCursor); close->setFixedSize(32, 32);
    connect(close, &QPushButton::clicked, this, &ModalOverlay::closeAnimated);
    hh->addWidget(title); hh->addStretch(); hh->addWidget(close);
    lay->addWidget(head);

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(14, 12, 14, 14);
    v->setSpacing(10);

    search_ = new QLineEdit();
    search_->setPlaceholderText(QStringLiteral("Поиск людей по @имени…"));
    v->addWidget(search_);

    status_ = new QLabel();
    status_->setObjectName(QStringLiteral("sub"));
    status_->setVisible(false);
    v->addWidget(status_);

    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listW = new QWidget();
    listBox_ = new QVBoxLayout(listW);
    listBox_->setContentsMargins(0, 0, 6, 0);
    listBox_->setSpacing(4);
    listBox_->addStretch();
    sa->setWidget(listW);
    v->addWidget(sa, 1);

    lay->addWidget(body, 1);

    debounce_ = new QTimer(this);
    debounce_->setSingleShot(true);
    debounce_->setInterval(320);
    connect(debounce_, &QTimer::timeout, this, [this]() {
        query_ = search_->text().trimmed();
        if (query_.isEmpty()) { results_.clear(); render(); }
        else api_->searchUsers(query_);
    });
    connect(search_, &QLineEdit::textChanged, this, [this]() { debounce_->start(); });

    connect(api_, &ApiClient::friendsLoaded, this, [this](const QList<UserHit>& f) {
        friends_ = f; if (query_.isEmpty()) render();
    });
    connect(api_, &ApiClient::friendRequestsLoaded, this, [this](const QList<FriendRequest>& r) {
        requests_ = r; if (query_.isEmpty()) render();
    });
    connect(api_, &ApiClient::usersFound, this, [this](const QString& q, const QList<UserHit>& u) {
        if (q != query_) return;
        results_ = u; render();
    });
    connect(api_, &ApiClient::friendRequestSent, this, [this](const QString&, bool ok, const QString& m) {
        status_->setVisible(true);
        status_->setText(ok ? QStringLiteral("Заявка отправлена") : (QStringLiteral("Ошибка: ") + m));
    });
    connect(api_, &ApiClient::friendActionDone, this, [this](const QString&, bool, bool ok) {
        if (ok) { api_->getFriendRequests(); api_->getFriends(); emit friendsChanged(); }
    });
    connect(api_, &ApiClient::contactActionDone, this, [this](bool ok) {
        if (ok) { api_->getFriends(); emit friendsChanged(); }
    });

    api_->getFriends();
    api_->getFriendRequests();
    render();
}

bool ContactsPanel::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            const QString id = w->property("openId").toString();
            if (!id.isEmpty()) {
                emit openChatRequested(id, w->property("openName").toString(),
                                       w->property("openUser").toString());
                closeAnimated();
                return true;
            }
        }
    }
    if (e->type() == QEvent::ContextMenu) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            const QString id = w->property("openId").toString();
            if (!id.isEmpty()) {
                QMenu menu(this);
                QAction* write = menu.addAction(QStringLiteral("Написать"));
                QAction* rm = menu.addAction(QStringLiteral("Удалить из контактов"));
                QAction* block = menu.addAction(QStringLiteral("Заблокировать"));
                QAction* ch = menu.exec(static_cast<QContextMenuEvent*>(e)->globalPos());
                if (ch == write) { emit openChatRequested(id, w->property("openName").toString(),
                                                          w->property("openUser").toString()); closeAnimated(); }
                else if (ch == rm)    { api_->removeFriend(id); }
                else if (ch == block) { api_->blockUser(id); }
                return true;
            }
        }
    }
    return ModalOverlay::eventFilter(obj, e);
}

void ContactsPanel::addSectionLabel(const QString& text) {
    auto* l = new QLabel(text);
    l->setObjectName(QStringLiteral("secLabel"));
    l->setContentsMargins(6, 8, 0, 2);
    listBox_->insertWidget(listBox_->count() - 1, l);
}

void ContactsPanel::addPersonRow(const UserHit& u, bool searchMode) {
    auto* row = new QFrame();
    row->setObjectName(QStringLiteral("personRow"));
    row->setCursor(Qt::PointingHandCursor);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 10, 6);
    h->setSpacing(10);

    auto* av = new QLabel(); av->setFixedSize(44, 44);
    Avatar::setRound(av, u.avatarUrl, u.displayName.isEmpty() ? u.username : u.displayName, 44);
    h->addWidget(av);

    auto* mid = new QVBoxLayout(); mid->setSpacing(1);
    QString nm = u.displayName.isEmpty() ? u.username : u.displayName;
    if (u.isPremium) nm += QStringLiteral("  ★");
    auto* name = new QLabel(nm); name->setObjectName(QStringLiteral("name"));
    auto* sub = new QLabel(u.online ? QStringLiteral("в сети")
                                    : (QStringLiteral("@") + u.username));
    sub->setObjectName(u.online ? QStringLiteral("online") : QStringLiteral("sub"));
    mid->addWidget(name); mid->addWidget(sub);
    h->addLayout(mid, 1);

    const bool isFriend = u.isFriend || [&]{ for (const UserHit& f : friends_) if (f.id == u.id) return true; return false; }();
    if (searchMode && !isFriend) {
        auto* add = new QPushButton(QStringLiteral("Добавить"));
        add->setObjectName(QStringLiteral("addBtn"));
        add->setCursor(Qt::PointingHandCursor);
        const QString uname = u.username;
        connect(add, &QPushButton::clicked, this, [this, uname]() { api_->sendFriendRequest(uname); });
        h->addWidget(add, 0, Qt::AlignVCenter);
    }

    // Клик по строке — открыть чат.
    const QString id = u.id, dn = nm, un = u.username;
    row->installEventFilter(this);
    row->setProperty("openId", id);
    row->setProperty("openName", u.displayName.isEmpty() ? u.username : u.displayName);
    row->setProperty("openUser", un);

    listBox_->insertWidget(listBox_->count() - 1, row);
}

void ContactsPanel::addRequestRow(const FriendRequest& r) {
    auto* row = new QFrame();
    row->setObjectName(QStringLiteral("personRow"));
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 10, 6);
    h->setSpacing(10);

    auto* av = new QLabel(); av->setFixedSize(44, 44);
    Avatar::setRound(av, QString(), r.senderUsername, 44);
    h->addWidget(av);

    auto* mid = new QVBoxLayout(); mid->setSpacing(1);
    auto* name = new QLabel(r.senderUsername); name->setObjectName(QStringLiteral("name"));
    auto* sub = new QLabel(QStringLiteral("хочет добавить вас")); sub->setObjectName(QStringLiteral("sub"));
    mid->addWidget(name); mid->addWidget(sub);
    h->addLayout(mid, 1);

    auto* ok = new QPushButton(QStringLiteral("✓")); ok->setObjectName(QStringLiteral("okBtn"));
    ok->setCursor(Qt::PointingHandCursor);
    auto* no = new QPushButton(QStringLiteral("✕")); no->setObjectName(QStringLiteral("noBtn"));
    no->setCursor(Qt::PointingHandCursor);
    const QString rid = r.id;
    connect(ok, &QPushButton::clicked, this, [this, rid]() { api_->acceptFriend(rid); });
    connect(no, &QPushButton::clicked, this, [this, rid]() { api_->rejectFriend(rid); });
    h->addWidget(ok); h->addWidget(no);

    listBox_->insertWidget(listBox_->count() - 1, row);
}

void ContactsPanel::render() {
    while (listBox_->count() > 1) {
        QLayoutItem* it = listBox_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }

    if (!query_.isEmpty()) {
        if (results_.isEmpty()) {
            auto* e = new QLabel(QStringLiteral("Никого не найдено"));
            e->setObjectName(QStringLiteral("empty"));
            listBox_->insertWidget(0, e);
            return;
        }
        for (const UserHit& u : results_) addPersonRow(u, /*searchMode*/true);
        return;
    }

    if (!requests_.isEmpty()) {
        addSectionLabel(QStringLiteral("ЗАЯВКИ"));
        for (const FriendRequest& r : requests_) addRequestRow(r);
    }

    addSectionLabel(QStringLiteral("КОНТАКТЫ"));
    if (friends_.isEmpty()) {
        auto* e = new QLabel(QStringLiteral("Пока нет контактов. Найдите людей через поиск выше."));
        e->setObjectName(QStringLiteral("empty"));
        e->setWordWrap(true);
        listBox_->insertWidget(listBox_->count() - 1, e);
    } else {
        for (const UserHit& u : friends_) addPersonRow(u, /*searchMode*/false);
    }
}
