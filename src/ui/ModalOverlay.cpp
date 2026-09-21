#include "ui/ModalOverlay.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QCoreApplication>
#include <QGraphicsDropShadowEffect>

ModalOverlay::ModalOverlay(QWidget* parent, int cardWidth)
    : QWidget(parent), cardWidth_(cardWidth) {
    setAttribute(Qt::WA_StyledBackground, false);
    raise();

    card_ = new QWidget(this);
    card_->setObjectName(QStringLiteral("modalCard"));
    card_->setFixedWidth(cardWidth_);
    card_->setStyleSheet(QStringLiteral(
        "#modalCard{background:#17151E;border:1px solid rgba(255,255,255,0.08);border-radius:18px;}"));
    auto* shadow = new QGraphicsDropShadowEffect(card_);
    shadow->setBlurRadius(60);
    shadow->setOffset(0, 18);
    shadow->setColor(QColor(0, 0, 0, 160));
    card_->setGraphicsEffect(shadow);

    cardLayout_ = new QVBoxLayout(card_);
    cardLayout_->setContentsMargins(22, 20, 22, 20);
    cardLayout_->setSpacing(12);

    // Центрируем карточку через layout — она авто-растёт под контент (пункты и т.п.).
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->addStretch();
    auto* row = new QHBoxLayout();
    row->addStretch();
    row->addWidget(card_);
    row->addStretch();
    root->addLayout(row);
    root->addStretch();

    if (parent) parent->installEventFilter(this);   // следим за ресайзом окна
    setGeometry(parent ? parent->rect() : QRect());
}

void ModalOverlay::relayout() {
    if (!parentWidget()) return;
    setGeometry(parentWidget()->rect());

    // Адаптация под маленькое окно (как min(1060px, 96vw) в вебе):
    // карточка ≤ 96% ширины и ≤ 94% высоты родителя.
    const int availW = qMax(120, int(width() * 0.96));
    const int availH = qMax(120, int(height() * 0.94));
    // Предпочтительная высота фиксируется при первом проходе (диалоги задают
    // её через card()->setFixedHeight(...) в конструкторе).
    if (cardPrefH_ == 0) cardPrefH_ = card_->minimumHeight();
    card_->setFixedWidth(qMin(cardWidth_, availW));
    const int h = cardPrefH_ > 0 ? qMin(cardPrefH_, availH) : 0;
    if (h > 0) {
        card_->setFixedHeight(h);
    } else {
        card_->setMinimumHeight(0);
        card_->setMaximumHeight(availH);   // контент задаёт высоту, но не выше
    }

    // Диалоги перестраиваются под ФИНАЛЬНЫЙ размер карточки: событие ресайза
    // самого оверлея могло прийти до клампа — досылаем его явно.
    QResizeEvent re(size(), size());
    QCoreApplication::sendEvent(this, &re);
}

bool ModalOverlay::eventFilter(QObject* obj, QEvent* e) {
    if (obj == parentWidget() && e->type() == QEvent::Resize) relayout();
    return QWidget::eventFilter(obj, e);
}

void ModalOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(0, 0, 0, int(150 * dim_)));   // затемнение фона
}

void ModalOverlay::mousePressEvent(QMouseEvent* e) {
    // Клик вне карточки → закрыть.
    if (!card_->geometry().contains(e->pos())) closeAnimated();
}

void ModalOverlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) closeAnimated();
    else QWidget::keyPressEvent(e);
}

void ModalOverlay::showAnimated() {
    show();
    raise();
    setFocus();
    relayout();

    auto* fade = new QPropertyAnimation(this, "dim", this);
    fade->setDuration(160);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void ModalOverlay::closeAnimated() {
    auto* fade = new QPropertyAnimation(this, "dim", this);
    fade->setDuration(140);
    fade->setStartValue(dim_);
    fade->setEndValue(0.0);
    connect(fade, &QPropertyAnimation::finished, this, [this]() {
        emit closed();
        deleteLater();
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}
