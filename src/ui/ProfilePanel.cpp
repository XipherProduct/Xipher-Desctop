#include "ui/ProfilePanel.h"
#include "ui/AvatarUtil.h"
#include "ui/Icons.h"
#include "net/ApiClient.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPropertyAnimation>
#include <QJsonObject>

ProfilePanel::ProfilePanel(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    setGeometry(parent->rect());
    parent->installEventFilter(this);

    panel_ = new QWidget(this);
    panel_->setObjectName(QStringLiteral("profilePanel"));
    panel_->setStyleSheet(QStringLiteral(
        "#profilePanel{background:#131218;border-left:1px solid rgba(255,255,255,0.10);}"));

    auto* root = new QVBoxLayout(panel_);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Шапка панели: назад + «Профиль».
    auto* head = new QWidget(panel_);
    head->setFixedHeight(56);
    head->setStyleSheet(QStringLiteral("border-bottom:1px solid rgba(255,255,255,0.08);"));
    auto* hl = new QHBoxLayout(head);
    hl->setContentsMargins(8, 0, 16, 0);
    auto* back = new QPushButton(head);
    back->setCursor(Qt::PointingHandCursor);
    back->setIcon(Icons::icon(Icons::Plus, 18, QColor(0xAC, 0xA6, 0xBD)));   // повернём как «×» ниже
    back->setText(QStringLiteral("✕"));
    back->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;background:transparent;color:#ACA6BD;font-size:18px;min-width:36px;min-height:36px;border-radius:8px;}"
        "QPushButton:hover{background:#221F2C;color:#F3F1F8;}"));
    back->setIcon(QIcon());
    auto* title = new QLabel(QStringLiteral("Профиль"), head);
    title->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:15px;font-weight:700;"));
    hl->addWidget(back);
    hl->addWidget(title);
    hl->addStretch();
    connect(back, &QPushButton::clicked, this, &ProfilePanel::closePanel);
    root->addWidget(head);

    // Прокручиваемое тело.
    auto* scroll = new QScrollArea(panel_);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea{border:none;background:transparent;}"
        "QScrollBar:vertical{background:transparent;width:8px;margin:2px;}"
        "QScrollBar::handle:vertical{background:rgba(255,255,255,0.14);border-radius:4px;min-height:30px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"));
    auto* body = new QWidget();
    body->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* bl = new QVBoxLayout(body);
    bl->setContentsMargins(0, 24, 0, 24);
    bl->setSpacing(0);

    avatar_ = new QLabel(body);
    avatar_->setAlignment(Qt::AlignCenter);
    name_ = new QLabel(body);
    name_->setAlignment(Qt::AlignCenter);
    name_->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:20px;font-weight:800;"));
    status_ = new QLabel(body);
    status_->setAlignment(Qt::AlignCenter);
    status_->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;"));

    bl->addWidget(avatar_, 0, Qt::AlignHCenter);
    bl->addSpacing(14);
    bl->addWidget(name_);
    bl->addSpacing(4);
    bl->addWidget(status_);
    bl->addSpacing(20);

    // Кнопка «Написать» 1:1 с .xp-act веба: surface-2 фон, hover surface-3, акцент.
    auto* msgBtn = new QPushButton(QStringLiteral("Написать сообщение"), body);
    msgBtn->setCursor(Qt::PointingHandCursor);
    msgBtn->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:20px;min-height:48px;color:#BBA4FF;"
        "font-weight:600;font-size:14px;background:#1A1822;}"
        "QPushButton:hover{background:#221F2C;}"));
    auto* msgWrap = new QHBoxLayout();
    msgWrap->setContentsMargins(20, 0, 20, 0);
    msgWrap->addWidget(msgBtn);
    bl->addLayout(msgWrap);
    bl->addSpacing(18);
    connect(msgBtn, &QPushButton::clicked, this, [this]() {
        emit messageRequested(userId_);
        closePanel();
    });

    infoBox_ = new QVBoxLayout();
    infoBox_->setContentsMargins(16, 0, 16, 0);
    infoBox_->setSpacing(12);
    // Секция-карточка 1:1 с .xp-sec веба: surface-2, r20, паддинг 8.
    auto* sec = new QWidget();
    sec->setObjectName(QStringLiteral("profileSec"));
    sec->setStyleSheet(QStringLiteral(
        "#profileSec{background:#1A1822;border-radius:20px;}"));
    auto* secL = new QVBoxLayout(sec);
    secL->setContentsMargins(8, 8, 8, 8);
    secL->setSpacing(0);
    secL->addLayout(infoBox_);
    secL->addStretch();
    bl->addWidget(sec);
    bl->addStretch();

    scroll->setWidget(body);
    root->addWidget(scroll, 1);

    connect(api_, &ApiClient::profileLoaded, this, &ProfilePanel::onProfileLoaded);
    hide();
}

void ProfilePanel::addInfoRow(const QString& value, const QString& caption) {
    if (value.isEmpty()) return;
    // Строка 1:1 с .xp-row веба: hover surface-3 r12, значение .95rem, лейбл .75rem.
    auto* row = new QWidget();
    row->setStyleSheet(QStringLiteral(
        "QWidget{background:transparent;border-radius:12px;}"
        "QWidget:hover{background:#221F2C;}"));
    auto* rl = new QVBoxLayout(row);
    rl->setContentsMargins(8, 8, 8, 8);
    rl->setSpacing(2);
    auto* v = new QLabel(value, row);
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:14px;"));   // .95rem
    auto* c = new QLabel(caption, row);
    c->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));   // .75rem
    rl->addWidget(v);
    rl->addWidget(c);
    infoBox_->addWidget(row);
}

void ProfilePanel::openFor(const QString& userId) {
    userId_ = userId;
    // плейсхолдер пока грузится
    Avatar::setRound(avatar_, QString(), QStringLiteral("?"), 96);
    name_->setText(QStringLiteral("Загрузка…"));
    status_->clear();
    while (QLayoutItem* it = infoBox_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    show();
    raise();
    setFocus();
    layoutPanel(true);
    api_->getUserProfile(userId);
}

void ProfilePanel::onProfileLoaded(const QJsonObject& obj) {
    const QJsonObject u = obj.value(QStringLiteral("user")).toObject();
    if (u.value(QStringLiteral("id")).toString() != userId_) return;

    const QString display = u.value(QStringLiteral("display_name")).toString();
    const QString uname = u.value(QStringLiteral("username")).toString();
    Avatar::setRound(avatar_, u.value(QStringLiteral("avatar_url")).toString(),
                     display.isEmpty() ? uname : display, 96);
    QString nm = display.isEmpty() ? uname : display;
    if (u.value(QStringLiteral("is_premium")).toBool(false)) nm += QStringLiteral("  ★");
    name_->setText(nm);
    status_->setText(u.value(QStringLiteral("is_online")).toBool(false)
                         ? QStringLiteral("в сети") : QStringLiteral("был(а) недавно"));
    if (u.value(QStringLiteral("is_online")).toBool(false))
        status_->setStyleSheet(QStringLiteral("color:#46B98A;font-size:13px;"));
    else
        status_->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;"));

    while (QLayoutItem* it = infoBox_->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    addInfoRow(u.value(QStringLiteral("bio")).toString(), QStringLiteral("О себе"));
    if (!uname.isEmpty()) addInfoRow(QStringLiteral("@") + uname, QStringLiteral("Имя пользователя"));
    const QString nft = u.value(QStringLiteral("nft_username")).toString();
    if (!nft.isEmpty()) addInfoRow(QStringLiteral("@") + nft, QStringLiteral("NFT-имя"));
    const int d = u.value(QStringLiteral("birth_day")).toInt();
    const int mo = u.value(QStringLiteral("birth_month")).toInt();
    if (d > 0 && mo > 0) {
        static const char* months[] = {"", "января","февраля","марта","апреля","мая","июня",
            "июля","августа","сентября","октября","ноября","декабря"};
        addInfoRow(QStringLiteral("%1 %2").arg(d).arg(QString::fromUtf8(months[qBound(1, mo, 12)])),
                   QStringLiteral("День рождения"));
    }
}

void ProfilePanel::layoutPanel(bool animate) {
    setGeometry(parentWidget()->rect());
    panel_->resize(panelW_, height());
    const int shown = width() - panelW_;
    if (animate) {
        panel_->move(width(), 0);
        auto* slide = new QPropertyAnimation(panel_, "pos", this);
        slide->setDuration(220);
        slide->setStartValue(QPoint(width(), 0));
        slide->setEndValue(QPoint(shown, 0));
        slide->setEasingCurve(QEasingCurve::OutCubic);
        slide->start(QAbstractAnimation::DeleteWhenStopped);
        auto* fade = new QPropertyAnimation(this, "dim", this);
        fade->setDuration(200);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        fade->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        panel_->move(shown, 0);
    }
}

void ProfilePanel::closePanel() {
    auto* slide = new QPropertyAnimation(panel_, "pos", this);
    slide->setDuration(180);
    slide->setStartValue(panel_->pos());
    slide->setEndValue(QPoint(width(), 0));
    slide->setEasingCurve(QEasingCurve::InCubic);
    slide->start(QAbstractAnimation::DeleteWhenStopped);
    auto* fade = new QPropertyAnimation(this, "dim", this);
    fade->setDuration(180);
    fade->setStartValue(dim_);
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, this, [this]() { hide(); });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void ProfilePanel::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, int(120 * dim_)));
}

void ProfilePanel::mousePressEvent(QMouseEvent* e) {
    if (!panel_->geometry().contains(e->pos())) closePanel();
}

void ProfilePanel::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) closePanel();
    else QWidget::keyPressEvent(e);
}

bool ProfilePanel::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize && isVisible())
        layoutPanel(false);
    return QWidget::eventFilter(obj, e);
}
