#include "ui/Theme.h"
#include "ui/PeerInfoPanel.h"
#include "ui/AvatarUtil.h"
#include "ui/ToggleSwitch.h"
#include "net/ApiClient.h"
#include "net/Session.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QMenu>
#include <QJsonObject>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

namespace {
QFrame* makeCard(QVBoxLayout*& body) {
    auto* c = new QFrame();
    c->setObjectName(QStringLiteral("card"));
    body = new QVBoxLayout(c);
    body->setContentsMargins(16, 14, 16, 14);
    body->setSpacing(10);
    return c;
}
QLabel* sec(const QString& t) {
    auto* l = new QLabel(t);
    l->setObjectName(QStringLiteral("sec"));
    return l;
}
}

PeerInfoPanel::PeerInfoPanel(ApiClient* api, const QString& peerId, bool isChannel,
                             const QString& name, const QString& avatarUrl, QWidget* parent)
    : ModalOverlay(parent, 460), api_(api), peerId_(peerId), isChannel_(isChannel),
      name_(name), avatarUrl_(avatarUrl) {
    card()->setFixedHeight(600);
    card()->setStyleSheet(ThemePreset::applyTokens(QStringLiteral(R"QSS(
#modalCard{background:@{s1};border:1px solid @{bDef};border-radius:18px;}
QLabel{color:@{tp};}
#cover{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 @{acD},stop:0.5 @{ac},stop:1 #B06CF0);
  border-top-left-radius:18px;border-top-right-radius:18px;}
#coverName{font-size:21px;font-weight:800;color:#fff;}
#coverSub{font-size:13px;color:rgba(255,255,255,0.88);}
#closeBtn{background:@{hovW2};border:none;border-radius:15px;color:#fff;font-size:15px;font-weight:700;}
#closeBtn:hover{background:rgba(255,255,255,0.32);}
#card{background:@{s2};border:1px solid @{bSub};border-radius:16px;}
#sec{color:@{acT};font-size:12px;font-weight:800;letter-spacing:1px;}
#desc{font-size:14px;color:@{ts};}
#infoCaption{color:@{tt};font-size:12px;}
#infoValue{color:@{tp};font-size:14px;}
#mName{color:@{tp};font-size:14px;font-weight:600;}
#mRole{color:@{ac};font-size:11px;font-weight:700;}
QLineEdit,QPlainTextEdit{background:@{s1};border:1px solid rgba(255,255,255,0.10);border-radius:10px;
  min-height:36px;padding:0 12px;color:@{tp};selection-background-color:@{ac};}
QPlainTextEdit{padding:8px 12px;}
QLineEdit:focus,QPlainTextEdit:focus{border:1px solid @{ac};}
#primaryBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 @{ac},stop:1 @{acD});
  color:#fff;border:none;border-radius:10px;min-height:38px;padding:0 18px;font-weight:700;}
#primaryBtn:hover{background:#9B72F8;}
#ghostBtn{background:@{s3};color:@{tp};border:none;border-radius:10px;min-height:38px;padding:0 16px;}
#ghostBtn:hover{background:#2C2838;}
#dangerBtn{background:transparent;color:#E5687A;border:1px solid rgba(229,104,122,0.4);
  border-radius:10px;min-height:38px;padding:0 16px;}
#dangerBtn:hover{background:rgba(229,104,122,0.12);}
#memberRow{background:transparent;border-radius:10px;}
#memberRow:hover{background:@{s3};}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:@{bStr};border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
)QSS")));

    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget();
    body_ = new QVBoxLayout(content);
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(0);
    body_->addStretch();
    sa->setWidget(content);
    lay->addWidget(sa, 1);

    // Плавающая кнопка закрытия поверх обложки.
    auto* close = new QPushButton(QStringLiteral("✕"), card());
    close->setObjectName(QStringLiteral("closeBtn"));
    close->setCursor(Qt::PointingHandCursor);
    close->setFixedSize(30, 30);
    close->move(card()->width() - 42, 12);
    close->raise();
    connect(close, &QPushButton::clicked, this, &ModalOverlay::closeAnimated);

    connect(api_, &ApiClient::channelInfoLoaded, this, &PeerInfoPanel::onChannelInfo);
    connect(api_, &ApiClient::membersLoaded, this, &PeerInfoPanel::onMembers);
    connect(api_, &ApiClient::peerActionDone, this, [this](bool ok, const QString& m) {
        if (!ok && !m.isEmpty()) QMessageBox::warning(this, QStringLiteral("Ошибка"), m);
        // обновим данные
        if (isChannel_) api_->getChannelInfo(peerId_);
        api_->getMembers(peerId_, isChannel_);
        emit changed();
    });
    connect(api_, &ApiClient::inviteLinkReady, this, [this](const QString& url) {
        if (url.isEmpty()) { QMessageBox::warning(this, QStringLiteral("Ссылка"), QStringLiteral("Не удалось создать ссылку")); return; }
        QApplication::clipboard()->setText(url);
        QMessageBox::information(this, QStringLiteral("Приглашение"),
            QStringLiteral("Ссылка скопирована:\n%1").arg(url));
    });

    if (!isChannel_) {
        connect(api_, &ApiClient::topicsLoaded, this, [this](const QString& gid, bool forum, const QList<Topic>&) {
            if (gid != peerId_) return;
            forumMode_ = forum; rebuild();
        });
        connect(api_, &ApiClient::topicActionDone, this, [this](bool ok, const QString&) {
            if (ok) api_->getGroupTopics(peerId_);
        });
        api_->getGroupTopics(peerId_);
    }

    if (isChannel_) api_->getChannelInfo(peerId_);
    api_->getMembers(peerId_, isChannel_);
    rebuild();
}

void PeerInfoPanel::onChannelInfo(const QJsonObject& obj) {
    const QJsonObject ch = obj.value(QStringLiteral("channel")).toObject();
    if (ch.value(QStringLiteral("id")).toString() != peerId_) return;
    name_        = ch.value(QStringLiteral("name")).toString();
    description_ = ch.value(QStringLiteral("description")).toString();
    customLink_  = ch.value(QStringLiteral("custom_link")).toString();
    avatarUrl_   = ch.value(QStringLiteral("avatar_url")).toString();
    const QString role = obj.value(QStringLiteral("user_role")).toString();
    isCreator_  = (role == QStringLiteral("creator") || role == QStringLiteral("owner"));
    canManage_  = isCreator_ || role == QStringLiteral("admin")
                  || obj.value(QStringLiteral("is_admin")).toBool(false);
    subscribed_ = obj.value(QStringLiteral("is_subscribed")).toBool(true);
    count_      = obj.value(QStringLiteral("subscribers_count")).toInt(
                    obj.value(QStringLiteral("total_members")).toInt());
    rebuild();
}

void PeerInfoPanel::onMembers(const QString& peerId, const QList<Member>& members) {
    if (peerId != peerId_) return;
    members_ = members;
    if (!isChannel_) {
        count_ = members.size();
        const QString myId = Session::instance().userId;
        for (const Member& m : members) if (m.id == myId) {
            isCreator_ = (m.role == QStringLiteral("creator"));
            canManage_ = isCreator_ || m.role == QStringLiteral("admin");
        }
    }
    rebuild();
}

void PeerInfoPanel::rebuild() {
    while (body_->count() > 1) {
        QLayoutItem* it = body_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    int at = 0;
    auto add = [&](QWidget* w) { body_->insertWidget(at++, w); };

    // ── Обложка: аватар по центру, имя, подпись ──────────────────────────────
    auto* cover = new QWidget();
    cover->setObjectName(QStringLiteral("cover"));
    cover->setAttribute(Qt::WA_StyledBackground, true);
    auto* cv = new QVBoxLayout(cover);
    cv->setContentsMargins(16, 30, 16, 18);
    cv->setSpacing(6);
    auto* av = new QLabel(); av->setFixedSize(96, 96);
    Avatar::setRound(av, avatarUrl_, name_, 96);
    cv->addWidget(av, 0, Qt::AlignHCenter);
    auto* nm = new QLabel(name_); nm->setObjectName(QStringLiteral("coverName"));
    nm->setAlignment(Qt::AlignHCenter); nm->setWordWrap(true);
    cv->addWidget(nm);
    QString sub = isChannel_ ? QStringLiteral("%1 подписчиков").arg(count_)
                             : QStringLiteral("%1 участников").arg(count_);
    if (!customLink_.isEmpty()) sub += QStringLiteral("   @") + customLink_;
    auto* sl = new QLabel(sub); sl->setObjectName(QStringLiteral("coverSub"));
    sl->setAlignment(Qt::AlignHCenter);
    cv->addWidget(sl);
    add(cover);

    // ── Контент в полях ──────────────────────────────────────────────────────
    auto* contentW = new QWidget();
    auto* body = new QVBoxLayout(contentW);
    body->setContentsMargins(14, 14, 14, 16);
    body->setSpacing(12);

    // Описание.
    if (!description_.isEmpty()) {
        QVBoxLayout* db = nullptr;
        auto* dc = makeCard(db);
        db->addWidget(sec(QStringLiteral("ОПИСАНИЕ")));
        auto* d = new QLabel(description_); d->setObjectName(QStringLiteral("desc")); d->setWordWrap(true);
        db->addWidget(d);
        body->addWidget(dc);
    }

    // Управление (создатель/админ).
    if (canManage_) {
        QVBoxLayout* mb = nullptr;
        auto* mgmt = makeCard(mb);
        mb->addWidget(sec(QStringLiteral("УПРАВЛЕНИЕ")));
        auto* nameEd = new QLineEdit(name_); nameEd->setMaxLength(128);
        auto* descEd = new QPlainTextEdit(description_); descEd->setFixedHeight(60);
        auto* linkEd = new QLineEdit(customLink_); linkEd->setPlaceholderText(QStringLiteral("публичная @ссылка"));
        mb->addWidget(new QLabel(QStringLiteral("Название")));
        mb->addWidget(nameEd);
        mb->addWidget(new QLabel(QStringLiteral("Описание")));
        mb->addWidget(descEd);
        mb->addWidget(new QLabel(QStringLiteral("Ссылка")));
        mb->addWidget(linkEd);
        auto* save = new QPushButton(QStringLiteral("Сохранить"));
        save->setObjectName(QStringLiteral("primaryBtn")); save->setCursor(Qt::PointingHandCursor);
        connect(save, &QPushButton::clicked, this, [this, nameEd, descEd, linkEd]() {
            if (nameEd->text().trimmed() != name_ && !nameEd->text().trimmed().isEmpty())
                api_->updatePeerName(peerId_, isChannel_, nameEd->text().trimmed());
            if (descEd->toPlainText() != description_)
                api_->updatePeerDescription(peerId_, isChannel_, descEd->toPlainText());
            QString l = linkEd->text().trimmed(); if (l.startsWith('@')) l = l.mid(1);
            if (l != customLink_)
                api_->setPeerCustomLink(peerId_, isChannel_, l);
        });
        mb->addWidget(save, 0, Qt::AlignRight);

        // Режим форума (только группы).
        if (!isChannel_) {
            auto* forumRow = new QWidget();
            auto* fh = new QHBoxLayout(forumRow); fh->setContentsMargins(0, 4, 0, 0); fh->setSpacing(12);
            auto* fl = new QLabel(QStringLiteral("Режим форума (темы)"));
            fl->setObjectName(QStringLiteral("desc"));
            auto* sw = new ToggleSwitch();
            sw->setChecked(forumMode_);
            connect(sw, &QAbstractButton::toggled, this, [this](bool on) {
                api_->setGroupForumMode(peerId_, on);
            });
            fh->addWidget(fl, 1); fh->addWidget(sw);
            mb->addWidget(forumRow);
        }

        auto* invite = new QPushButton(QStringLiteral("Создать ссылку-приглашение"));
        invite->setObjectName(QStringLiteral("ghostBtn")); invite->setCursor(Qt::PointingHandCursor);
        connect(invite, &QPushButton::clicked, this, [this]() { api_->createInvite(peerId_, isChannel_); });
        mb->addWidget(invite);
        body->addWidget(mgmt);
    }

    // Участники.
    QVBoxLayout* pb = nullptr;
    auto* pc = makeCard(pb);
    pb->addWidget(sec(isChannel_ ? QStringLiteral("ПОДПИСЧИКИ") : QStringLiteral("УЧАСТНИКИ")));
    const QString myId = Session::instance().userId;
    for (const Member& m : members_) {
        auto* row = new QFrame(); row->setObjectName(QStringLiteral("memberRow"));
        auto* h = new QHBoxLayout(row); h->setContentsMargins(8, 5, 8, 5); h->setSpacing(10);
        auto* av2 = new QLabel(); av2->setFixedSize(38, 38);
        Avatar::setRound(av2, QString(), m.username, 38);
        h->addWidget(av2);
        auto* col = new QVBoxLayout(); col->setSpacing(1);
        auto* un = new QLabel(m.username + (m.id == myId ? QStringLiteral("  (вы)") : QString()));
        un->setObjectName(QStringLiteral("mName"));
        col->addWidget(un);
        if (m.role == QStringLiteral("creator") || m.role == QStringLiteral("owner")
            || m.role == QStringLiteral("admin")) {
            auto* r = new QLabel(m.role == QStringLiteral("admin") ? QStringLiteral("админ") : QStringLiteral("создатель"));
            r->setObjectName(QStringLiteral("mRole"));
            col->addWidget(r);
        }
        h->addLayout(col, 1);

        // Действия над участником (для управляющего, не над собой/создателем).
        if (canManage_ && m.id != myId && m.role != QStringLiteral("creator") && m.role != QStringLiteral("owner")) {
            auto* act = new QPushButton(QStringLiteral("⋯")); act->setObjectName(QStringLiteral("ghostBtn"));
            act->setCursor(Qt::PointingHandCursor); act->setFixedWidth(40);
            const Member mm = m;
            connect(act, &QPushButton::clicked, this, [this, mm, act]() {
                QMenu menu(this);
                if (!isChannel_) {
                    QAction* promote = menu.addAction(mm.role == QStringLiteral("admin")
                        ? QStringLiteral("Снять админа") : QStringLiteral("Сделать админом"));
                    QAction* mute = menu.addAction(mm.isMuted ? QStringLiteral("Размутить") : QStringLiteral("Заглушить"));
                    QAction* kick = menu.addAction(QStringLiteral("Исключить"));
                    QAction* ch = menu.exec(act->mapToGlobal(QPoint(0, act->height())));
                    if (ch == promote)
                        api_->setGroupMemberRole(peerId_, mm.id,
                            mm.role == QStringLiteral("admin") ? QStringLiteral("member") : QStringLiteral("admin"));
                    else if (ch == mute) api_->muteGroupMember(peerId_, mm.id, !mm.isMuted);
                    else if (ch == kick) api_->kickGroupMember(peerId_, mm.id);
                } else {
                    QAction* ban = menu.addAction(mm.isBanned ? QStringLiteral("Разбанить") : QStringLiteral("Забанить"));
                    QAction* ch = menu.exec(act->mapToGlobal(QPoint(0, act->height())));
                    if (ch == ban) api_->banChannelMember(peerId_, mm.id, !mm.isBanned);
                }
            });
            h->addWidget(act, 0, Qt::AlignVCenter);
        }
        pb->addWidget(row);
    }
    body->addWidget(pc);

    // Действия внизу: подписка/выход/удаление.
    QVBoxLayout* ab = nullptr;
    auto* ac = makeCard(ab);
    if (isChannel_) {
        auto* unsub = new QPushButton(subscribed_ ? QStringLiteral("Отписаться") : QStringLiteral("Подписаться"));
        unsub->setObjectName(subscribed_ ? QStringLiteral("dangerBtn") : QStringLiteral("primaryBtn"));
        unsub->setCursor(Qt::PointingHandCursor);
        connect(unsub, &QPushButton::clicked, this, [this]() {
            if (subscribed_) { api_->unsubscribeChannel(peerId_); emit leftPeer(peerId_); closeAnimated(); }
            else api_->joinPublic(peerId_, QStringLiteral("channel"));
        });
        ab->addWidget(unsub);
    } else {
        auto* leave = new QPushButton(QStringLiteral("Выйти из группы"));
        leave->setObjectName(QStringLiteral("dangerBtn")); leave->setCursor(Qt::PointingHandCursor);
        connect(leave, &QPushButton::clicked, this, [this]() {
            api_->leaveGroup(peerId_); emit leftPeer(peerId_); closeAnimated();
        });
        ab->addWidget(leave);
    }
    if (isCreator_) {
        auto* del = new QPushButton(isChannel_ ? QStringLiteral("Удалить канал") : QStringLiteral("Удалить группу"));
        del->setObjectName(QStringLiteral("dangerBtn")); del->setCursor(Qt::PointingHandCursor);
        connect(del, &QPushButton::clicked, this, [this]() {
            if (QMessageBox::question(this, QStringLiteral("Удаление"),
                    QStringLiteral("Удалить безвозвратно?")) != QMessageBox::Yes) return;
            if (isChannel_) api_->deleteChannel(peerId_); else api_->deleteGroup(peerId_);
            emit leftPeer(peerId_); closeAnimated();
        });
        ab->addWidget(del);
    }
    body->addWidget(ac);
    add(contentW);
}
