#pragma once
#include <QPixmap>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Icons — простые векторные line-иконки (QPainter), вместо эмодзи-глифов.
//  Рисуются в виалбоксе 24×24 и масштабируются. Возвращают QPixmap/QIcon.
// ─────────────────────────────────────────────────────────────────────────────
namespace Icons {

enum Kind { Send, Mic, Smile, Paperclip, Clock, Trash, File, Image,
            Search, Pencil, Phone, Location, Checklist, Logout, Plus, More, Lock,
            Menu, Bell, Shield, User, Globe, Star, Device, Gear, Camera,
            ChevronRight, ArrowLeft, Block, Chat, Gift, Poll, Sparkle,
            // Строки профиля и его действия (SVG из js/profile/view.js веба).
            Message, Megaphone, Cake, At, Calendar, About,
            // Звонки (SVG из js/calls.js): трубка вниз — отбой, динамик.
            Hangup, Speaker };

inline QPixmap pixmap(Kind kind, int size, const QColor& color) {
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(size / 24.0, size / 24.0);

    QPen pen(color);
    pen.setWidthF(2.0);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case Send: {
        QPainterPath path;
        path.moveTo(3, 12); path.lineTo(21, 3); path.lineTo(14, 21);
        path.lineTo(11.5, 13.5); path.closeSubpath();
        p.setBrush(color); p.setPen(Qt::NoPen);
        p.drawPath(path);
        break;
    }
    case Mic: {
        p.drawRoundedRect(QRectF(9, 3, 6, 11), 3, 3);
        QPainterPath arc;
        arc.moveTo(6, 11); arc.arcTo(QRectF(6, 5, 12, 12), 180, 180);
        p.drawPath(arc);
        p.drawLine(QLineF(12, 18, 12, 21));
        p.drawLine(QLineF(9, 21, 15, 21));
        break;
    }
    case Smile: {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.setBrush(color);
        p.drawEllipse(QPointF(9, 10), 1.1, 1.1);
        p.drawEllipse(QPointF(15, 10), 1.1, 1.1);
        p.setBrush(Qt::NoBrush);
        QPainterPath smile;
        smile.moveTo(8, 14); smile.arcTo(QRectF(8, 10.5, 8, 6), 200, 140);
        p.drawPath(smile);
        break;
    }
    case Paperclip: {
        QPainterPath clip;
        clip.moveTo(16, 8);
        clip.lineTo(8.5, 15.5);
        clip.arcTo(QRectF(6, 13, 5, 5), 135, 180);
        clip.lineTo(17, 7);
        clip.arcTo(QRectF(15, 4.5, 5.5, 5.5), 135, 200);
        clip.lineTo(7, 16);
        p.drawPath(clip);
        break;
    }
    case Clock: {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawLine(QLineF(12, 12, 12, 7.5));
        p.drawLine(QLineF(12, 12, 15.5, 13.5));
        break;
    }
    case Trash: {
        p.drawLine(QLineF(4, 6.5, 20, 6.5));
        p.drawRoundedRect(QRectF(6, 6.5, 12, 14), 2, 2);
        p.drawLine(QLineF(9, 3.5, 15, 3.5));
        p.drawLine(QLineF(10, 10, 10, 17));
        p.drawLine(QLineF(14, 10, 14, 17));
        break;
    }
    case File: {
        QPainterPath f;
        f.moveTo(6, 3); f.lineTo(14, 3); f.lineTo(19, 8); f.lineTo(19, 21);
        f.lineTo(6, 21); f.closeSubpath();
        p.drawPath(f);
        p.drawPolyline(QPolygonF({QPointF(14, 3), QPointF(14, 8), QPointF(19, 8)}));
        break;
    }
    case Image: {
        p.drawRoundedRect(QRectF(3, 5, 18, 14), 2.5, 2.5);
        p.drawEllipse(QPointF(8.5, 10), 1.8, 1.8);
        QPainterPath m;
        m.moveTo(5, 18); m.lineTo(11, 12); m.lineTo(15, 16); m.lineTo(17, 14); m.lineTo(20, 18);
        p.drawPath(m);
        break;
    }
    case Search: {
        p.drawEllipse(QPointF(10.5, 10.5), 6.5, 6.5);
        p.drawLine(QLineF(15.2, 15.2, 20, 20));
        break;
    }
    case Pencil: {
        QPainterPath pp;
        pp.moveTo(4, 20); pp.lineTo(4, 16.5); pp.lineTo(15.5, 5);
        pp.lineTo(19, 8.5); pp.lineTo(7.5, 20); pp.closeSubpath();
        p.drawPath(pp);
        p.drawLine(QLineF(13.5, 7, 17, 10.5));
        break;
    }
    case Phone: {
        QPainterPath ph;
        ph.moveTo(5, 4); ph.lineTo(9, 4); ph.lineTo(11, 9); ph.lineTo(8.5, 11);
        ph.arcTo(QRectF(8.5, 11, 9, 9), 90, -90);
        ph.lineTo(15, 13); ph.lineTo(20, 15); ph.lineTo(20, 19);
        ph.arcTo(QRectF(4, 4, 16, 16), 0, 0);
        p.drawPath(ph);
        break;
    }
    case Location: {
        QPainterPath pin;
        pin.moveTo(12, 21);
        pin.arcTo(QRectF(5, 2, 14, 14), -60, 300);
        pin.closeSubpath();
        p.drawPath(pin);
        p.drawEllipse(QPointF(12, 9), 2.4, 2.4);
        break;
    }
    case Checklist: {
        QPen tp2 = pen; tp2.setWidthF(2.2); p.setPen(tp2);
        p.drawPolyline(QPolygonF({QPointF(3, 6), QPointF(4.5, 7.5), QPointF(7, 4.5)}));
        p.drawPolyline(QPolygonF({QPointF(3, 13), QPointF(4.5, 14.5), QPointF(7, 11.5)}));
        p.drawPolyline(QPolygonF({QPointF(3, 20), QPointF(4.5, 21.5), QPointF(7, 18.5)}));
        p.drawLine(QLineF(10, 6, 21, 6));
        p.drawLine(QLineF(10, 13, 21, 13));
        p.drawLine(QLineF(10, 20, 21, 20));
        break;
    }
    case Logout: {
        QPainterPath d;
        d.moveTo(13, 4); d.lineTo(6, 4); d.lineTo(6, 20); d.lineTo(13, 20);
        p.drawPath(d);
        p.drawLine(QLineF(11, 12, 21, 12));
        p.drawPolyline(QPolygonF({QPointF(17.5, 8.5), QPointF(21, 12), QPointF(17.5, 15.5)}));
        break;
    }
    case Plus: {
        p.drawLine(QLineF(12, 5, 12, 19));
        p.drawLine(QLineF(5, 12, 19, 12));
        break;
    }
    case Poll: {
        // Опрос: три столбика разной высоты (bar-chart).
        p.drawLine(QLineF(6, 13, 6, 19));
        p.drawLine(QLineF(12, 6, 12, 19));
        p.drawLine(QLineF(18, 10, 18, 19));
        break;
    }
    case Sparkle: {
        // «Формат» (rich text): четырёхлучевая искра.
        QPainterPath sp;
        sp.moveTo(12, 3.5);
        sp.quadTo(13.4, 10.6, 20.5, 12);
        sp.quadTo(13.4, 13.4, 12, 20.5);
        sp.quadTo(10.6, 13.4, 3.5, 12);
        sp.quadTo(10.6, 10.6, 12, 3.5);
        p.drawPath(sp);
        break;
    }
    case Chat: {
        // Бабл сообщения (message-circle веба): контур + хвост.
        p.drawRoundedRect(QRectF(3.5, 4.5, 17, 11.5), 4.5, 4.5);
        p.drawPolyline(QPolygonF({QPointF(8.8, 15.6), QPointF(8.8, 20),
                                  QPointF(13.2, 15.7)}));
        break;
    }
    case Gift: {
        // Подарок: коробка + крышка + лента + бант (gift веба).
        p.drawRoundedRect(QRectF(3.8, 10, 16.4, 10.2), 2, 2);
        p.drawRoundedRect(QRectF(2.8, 6.8, 18.4, 3.5), 1.7, 1.7);
        p.drawLine(QLineF(12, 6.8, 12, 20.2));
        p.drawEllipse(QPointF(9.2, 5.1), 2.5, 1.9);
        p.drawEllipse(QPointF(14.8, 5.1), 2.5, 1.9);
        break;
    }
    case More: {
        p.setBrush(color); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(12, 5.5), 1.7, 1.7);
        p.drawEllipse(QPointF(12, 12), 1.7, 1.7);
        p.drawEllipse(QPointF(12, 18.5), 1.7, 1.7);
        break;
    }
    case Lock: {
        p.drawRoundedRect(QRectF(5, 10.5, 14, 10), 2.5, 2.5);
        QPainterPath sh;
        sh.moveTo(8, 10.5); sh.lineTo(8, 7.5);
        sh.arcTo(QRectF(8, 3.5, 8, 8), 180, -180);
        sh.lineTo(16, 10.5);
        p.drawPath(sh);
        break;
    }
    case Menu: {
        p.drawLine(QLineF(4, 6.5, 20, 6.5));
        p.drawLine(QLineF(4, 12, 20, 12));
        p.drawLine(QLineF(4, 17.5, 20, 17.5));
        break;
    }
    case Bell: {
        QPainterPath b;
        b.moveTo(6, 17); b.lineTo(6, 11);
        b.arcTo(QRectF(6, 4, 12, 12), 180, -180);
        b.lineTo(18, 17); b.closeSubpath();
        p.drawPath(b);
        p.drawLine(QLineF(12, 3, 12, 4.5));
        QPainterPath cl; cl.moveTo(10.3, 20); cl.arcTo(QRectF(10.3, 17.5, 3.4, 3.4), 180, 180);
        p.drawPath(cl);
        break;
    }
    case Shield: {
        QPainterPath s;
        s.moveTo(12, 3); s.lineTo(19, 6); s.lineTo(19, 12);
        s.arcTo(QRectF(5, 6, 14, 15), 60, 60);
        s.lineTo(5, 6); s.closeSubpath();
        p.drawPath(s);
        p.drawPolyline(QPolygonF({QPointF(9, 12), QPointF(11, 14), QPointF(15.5, 9.5)}));
        break;
    }
    case User: {
        p.drawEllipse(QPointF(12, 8.5), 4, 4);
        QPainterPath bd;
        bd.moveTo(4.5, 20); bd.arcTo(QRectF(4.5, 13, 15, 15), 180, -180);
        p.drawPath(bd);
        break;
    }
    case Globe: {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawEllipse(QPointF(12, 12), 4, 9);
        p.drawLine(QLineF(3, 12, 21, 12));
        break;
    }
    case Star: {
        QPainterPath st;
        for (int i = 0; i < 5; ++i) {
            const double ao = -90 + i * 144;
            const double r = ao * M_PI / 180.0;
            const QPointF pt(12 + 9 * std::cos(r), 12 + 9 * std::sin(r));
            if (i == 0) st.moveTo(pt); else st.lineTo(pt);
        }
        st.closeSubpath();
        p.setBrush(color); p.setPen(Qt::NoPen);
        p.drawPath(st);
        break;
    }
    case Device: {
        p.drawRoundedRect(QRectF(3, 5, 13, 10), 1.8, 1.8);
        p.drawLine(QLineF(7, 18, 12, 18));
        p.drawRoundedRect(QRectF(17, 8, 4, 11), 1.4, 1.4);
        break;
    }
    case Gear: {
        p.drawEllipse(QPointF(12, 12), 3, 3);
        for (int i = 0; i < 8; ++i) {
            const double a = i * 45 * M_PI / 180.0;
            const QPointF a1(12 + 6 * std::cos(a), 12 + 6 * std::sin(a));
            const QPointF a2(12 + 8.5 * std::cos(a), 12 + 8.5 * std::sin(a));
            p.drawLine(QLineF(a1, a2));
        }
        break;
    }
    case Camera: {
        QPainterPath bd;
        bd.moveTo(4, 8); bd.lineTo(8, 8); bd.lineTo(9.5, 6); bd.lineTo(14.5, 6);
        bd.lineTo(16, 8); bd.lineTo(20, 8); bd.lineTo(20, 19); bd.lineTo(4, 19);
        bd.closeSubpath();
        p.drawPath(bd);
        p.drawEllipse(QPointF(12, 13), 3.4, 3.4);
        break;
    }
    case ChevronRight: {
        p.drawPolyline(QPolygonF({QPointF(9.5, 5), QPointF(16, 12), QPointF(9.5, 19)}));
        break;
    }
    case ArrowLeft: {
        p.drawLine(QLineF(20, 12, 4, 12));
        p.drawPolyline(QPolygonF({QPointF(10.5, 5.5), QPointF(4, 12), QPointF(10.5, 18.5)}));
        break;
    }
    case Block: {
        p.drawEllipse(QPointF(12, 12), 9, 9);
        p.drawLine(QLineF(5.6, 5.6, 18.4, 18.4));
        break;
    }
    // Кривая стрелка-бабл (ICONS.message из view.js): дуга 8r с разрывом
    // справа-сверху + короткий хвостик слева-снизу.
    case Message: {
        QPainterPath arc;
        arc.moveTo(20, 12);
        arc.arcTo(QRectF(4, 4, 16, 16), 0, 308);
        p.drawPath(arc);
        p.drawLine(QLineF(4, 20, 5.4, 16.4));
        break;
    }
    // Мегафон персонального канала (ROW_ICONS.megaphone).
    case Megaphone: {
        QPainterPath sp;
        sp.moveTo(3, 10); sp.lineTo(14, 6); sp.lineTo(14, 18); sp.lineTo(3, 14);
        sp.closeSubpath();
        p.drawPath(sp);
        QPainterPath w;
        w.moveTo(14, 8);
        w.arcTo(QRectF(10, 8, 8, 8), 270, 180);
        p.drawPath(w);
        p.drawLine(QLineF(6, 14, 6, 18));
        break;
    }
    // Торт дня рождения (ROW_ICONS.cake): пламя + торт + волнистый низ.
    case Cake: {
        QPainterPath fl;
        fl.moveTo(12, 6);
        fl.cubicTo(13.2, 6, 14, 5.1, 14, 4);
        fl.cubicTo(14, 2.9, 12, 2, 12, 2);
        fl.cubicTo(12, 2, 10, 2.9, 10, 4);
        fl.cubicTo(10, 5.1, 10.8, 6, 12, 6);
        p.setPen(Qt::NoPen); p.setBrush(color); p.drawPath(fl);
        p.setPen(pen);
        p.drawRoundedRect(QRectF(5, 8, 14, 6), 3, 3);
        QPainterPath base;
        base.moveTo(4, 17);
        base.cubicTo(5.3, 18.2, 6.7, 18.2, 8, 17);
        base.cubicTo(9.3, 18.2, 10.7, 18.2, 12, 17);
        base.cubicTo(13.3, 18.2, 14.7, 18.2, 16, 17);
        base.cubicTo(17.3, 18.2, 18.7, 18.2, 20, 17);
        base.lineTo(20, 21); base.lineTo(4, 21); base.closeSubpath();
        p.drawPath(base);
        break;
    }
    // «Собака» имени пользователя (ROW_ICONS.at).
    case At: {
        p.drawEllipse(QPointF(12, 12), 4, 4);
        QPainterPath big;
        big.moveTo(21, 12);
        big.arcTo(QRectF(3, 3, 18, 18), 0, -308);
        p.drawPath(big);
        p.drawLine(QLineF(16, 12, 16, 13.5));
        QPainterPath hook;
        hook.moveTo(16, 13.5);
        hook.arcTo(QRectF(16, 11, 5, 5), 180, -180);
        p.drawPath(hook);
        break;
    }
    // Календарь «В Xipher с» (ROW_ICONS.calendar).
    case Calendar: {
        p.drawRoundedRect(QRectF(4, 6, 16, 14), 2, 2);
        p.drawLine(QLineF(8, 3, 8, 8));
        p.drawLine(QLineF(16, 3, 16, 8));
        p.drawLine(QLineF(4, 11, 20, 11));
        break;
    }
    // «i» у био (ROW_ICONS.about).
    case About: {
        p.drawEllipse(QPointF(12, 12), 8.5, 8.5);
        p.drawLine(QLineF(12, 11, 12, 16));
        p.setPen(Qt::NoPen); p.setBrush(color);
        p.drawEllipse(QPointF(12, 7.8), 1.1, 1.1);
        break;
    }
    // Трубка «отбой» (hangup из calls.js): телефон, повёрнутый на 135°.
    case Hangup: {
        p.translate(12, 12);
        p.rotate(135);
        p.translate(-12, -12);
        QPainterPath ph;
        ph.moveTo(5, 4); ph.lineTo(9, 4); ph.lineTo(11, 9); ph.lineTo(8.5, 11);
        ph.arcTo(QRectF(8.5, 11, 9, 9), 90, -90);
        ph.lineTo(15, 13); ph.lineTo(20, 15); ph.lineTo(20, 19);
        ph.arcTo(QRectF(4, 4, 16, 16), 0, 0);
        p.drawPath(ph);
        break;
    }
    // Динамик (speaker из calls.js): рупор + дуги звука.
    case Speaker: {
        QPainterPath sp;
        sp.moveTo(4, 9.5); sp.lineTo(8, 9.5); sp.lineTo(13, 5);
        sp.lineTo(13, 19); sp.lineTo(8, 14.5); sp.lineTo(4, 14.5);
        sp.closeSubpath();
        p.setBrush(color); p.setPen(Qt::NoPen);
        p.drawPath(sp);
        p.setPen(pen); p.setBrush(Qt::NoBrush);
        QPainterPath w1;
        w1.moveTo(16, 9.5);
        w1.arcTo(QRectF(15.2, 9.5, 4, 5), -80, 160);
        p.drawPath(w1);
        QPainterPath w2;
        w2.moveTo(18.5, 7);
        w2.arcTo(QRectF(16.5, 7, 7, 10), -80, 160);
        p.drawPath(w2);
        break;
    }
    }
    p.end();
    return pm;
}

inline QIcon icon(Kind kind, int size, const QColor& color) {
    return QIcon(pixmap(kind, size, color));
}

} // namespace Icons
