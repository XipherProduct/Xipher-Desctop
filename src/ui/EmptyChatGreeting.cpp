#include "ui/EmptyChatGreeting.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMovie>
#include <QFrame>
#include <QEvent>
#include <QGraphicsDropShadowEffect>

EmptyChatGreeting::EmptyChatGreeting(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch();

    // Карточка по центру (как «пустой чат» в Telegram).
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("greetCard"));
    card->setStyleSheet(QStringLiteral(
        "#greetCard{background:rgba(26,24,34,90%);border:1px solid rgba(255,255,255,7%);"
        "border-radius:18px;}"));
    card->setMaximumWidth(320);
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(40);
    shadow->setOffset(0, 12);
    shadow->setColor(QColor(0, 0, 0, 120));
    card->setGraphicsEffect(shadow);

    auto* cl = new QVBoxLayout(card);
    cl->setContentsMargins(28, 26, 28, 26);
    cl->setSpacing(10);

    // Анимированный эмодзи (Noto Animated Emoji через QMovie).
    emojiLabel_ = new QLabel(card);
    emojiLabel_->setAlignment(Qt::AlignCenter);
    emojiLabel_->setCursor(Qt::PointingHandCursor);
    emojiLabel_->setFixedSize(112, 112);
    emojiLabel_->setToolTip(QStringLiteral("Поздороваться"));
    movie_ = new QMovie(QStringLiteral(":/emoji/1f44b.gif"), QByteArray(), this);
    movie_->setScaledSize(QSize(104, 104));
    emojiLabel_->setMovie(movie_);
    movie_->start();
    emojiLabel_->installEventFilter(this);

    auto* title = new QLabel(QStringLiteral("Здесь пока ничего нет"), card);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:16px;font-weight:700;"));

    auto* sub = new QLabel(
        QStringLiteral("Отправьте сообщение ниже\nили нажмите на приветствие выше"), card);
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:13px;line-height:1.5;"));

    cl->addWidget(emojiLabel_, 0, Qt::AlignHCenter);
    cl->addWidget(title);
    cl->addWidget(sub);

    auto* hcenter = new QHBoxLayout();
    hcenter->addStretch();
    hcenter->addWidget(card);
    hcenter->addStretch();
    outer->addLayout(hcenter);
    outer->addStretch();
}

bool EmptyChatGreeting::eventFilter(QObject* obj, QEvent* e) {
    if (obj == emojiLabel_ && e->type() == QEvent::MouseButtonRelease) {
        emit greetingClicked(QStringLiteral("👋"));
        return true;
    }
    return QWidget::eventFilter(obj, e);
}
