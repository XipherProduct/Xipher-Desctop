#include "ui/TransferRing.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

TransferRing::TransferRing(int size, QWidget* parent)
    : QWidget(parent), size_(size) {
    setFixedSize(size, size);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("Скачать"));
}

void TransferRing::setState(State state) {
    state_ = state;
    switch (state_) {
        case State::Idle:    setToolTip(QStringLiteral("Скачать")); break;
        case State::Loading: setToolTip(QStringLiteral("Отменить")); break;
        case State::Done:    setToolTip(QStringLiteral("Открыть")); break;
    }
    update();
}

void TransferRing::setProgress(int percent) {
    progress_ = qBound(0, percent, 100);
    if (state_ == State::Idle && progress_ > 0) setState(State::Loading);
    update();
}

void TransferRing::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) {
        emit clicked();
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void TransferRing::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = rect();
    const QPointF c = r.center();
    const qreal radius = size_ / 2.0 - 2.4;

    // Тёмная подложка (читаемо и на баббле, и на фото).
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 120));
    p.drawEllipse(c, radius, radius);

    QPen trackPen(QColor(255, 255, 255, 36), 2.4);
    trackPen.setCapStyle(Qt::RoundCap);

    if (state_ == State::Loading) {
        p.setPen(trackPen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, radius, radius);
        QPen progPen(QColor(0x8B, 0x5C, 0xF6), 2.8);
        progPen.setCapStyle(Qt::RoundCap);
        p.setPen(progPen);
        const qreal frac = progress_ / 100.0;
        if (frac > 0.001) {
            const qreal span = -360.0 * frac;
            p.drawArc(QRectF(c.x() - radius, c.y() - radius, radius * 2, radius * 2),
                      90 * 16, int(span * 16));
        }
        drawCancelIcon(p, c);
    } else if (state_ == State::Done && doneFileStyle_) {
        drawFileIcon(p, c);
    } else {
        p.setPen(trackPen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, radius, radius);
        drawDownloadIcon(p, c);
    }
}

void TransferRing::drawDownloadIcon(QPainter& p, const QPointF& c) {
    QPen pen(Qt::white, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(c.x(), c.y() - 6), QPointF(c.x(), c.y() + 3));
    QPainterPath head;
    head.moveTo(c.x() - 4, c.y() - 1); head.lineTo(c.x(), c.y() + 4);
    head.lineTo(c.x() + 4, c.y() - 1);
    p.drawPath(head);
    p.drawLine(QPointF(c.x() - 6, c.y() + 7), QPointF(c.x() + 6, c.y() + 7));
}

void TransferRing::drawCancelIcon(QPainter& p, const QPointF& c) {
    QPen pen(Qt::white, 2.2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.drawLine(QPointF(c.x() - 4, c.y() - 4), QPointF(c.x() + 4, c.y() + 4));
    p.drawLine(QPointF(c.x() + 4, c.y() - 4), QPointF(c.x() - 4, c.y() + 4));
}

void TransferRing::drawFileIcon(QPainter& p, const QPointF& c) {
    QPen pen(Qt::white, 1.8);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    QPainterPath path;
    path.moveTo(c.x() - 5, c.y() - 7); path.lineTo(c.x() + 1, c.y() - 7);
    path.lineTo(c.x() + 5, c.y() - 3); path.lineTo(c.x() + 5, c.y() + 7);
    path.lineTo(c.x() - 5, c.y() + 7); path.closeSubpath();
    p.drawPath(path);
    p.drawLine(QPointF(c.x() + 1, c.y() - 7), QPointF(c.x() + 1, c.y() - 3));
    p.drawLine(QPointF(c.x() + 1, c.y() - 3), QPointF(c.x() + 5, c.y() - 3));
    p.drawLine(QPointF(c.x() - 3, c.y()), QPointF(c.x() + 3, c.y()));
    p.drawLine(QPointF(c.x() - 3, c.y() + 3), QPointF(c.x() + 3, c.y() + 3));
}
