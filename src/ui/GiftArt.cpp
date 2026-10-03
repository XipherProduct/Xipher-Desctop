#include "ui/GiftArt.h"

#include <QBuffer>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>

#ifdef XIPHER_QT_SVG
#include <QSvgRenderer>
#endif

namespace {

int indexOf(const QString& slug) {
    const GiftArtData::Entry* e = GiftArtData::entries();
    for (int i = 0; i < GiftArtData::count(); ++i)
        if (slug == QLatin1String(e[i].slug)) return i;
    return -1;
}

QMutex g_mutex;
QHash<QString, QPixmap> g_cache;   // ключ slug@px

} // namespace

bool GiftArt::known(const QString& slug) { return indexOf(slug) >= 0; }

QByteArray GiftArt::svg(const QString& slug) {
    const int i = indexOf(slug);
    if (i >= 0) return QByteArray(GiftArtData::entries()[i].svg);
    return QByteArray(GiftArtData::boxSvg());
}

QPixmap GiftArt::pixmap(const QString& slug, int px) {
    const QString key = QStringLiteral("%1@%2").arg(slug).arg(px);
    {
        QMutexLocker lock(&g_mutex);
        const auto it = g_cache.constFind(key);
        if (it != g_cache.constEnd()) return it.value();
    }
    const QByteArray markup = svg(slug);

    // 2× рендер под чёткость на hidpi.
    QImage img(px * 2, px * 2, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    bool painted = false;
#ifdef XIPHER_QT_SVG
    {
        QSvgRenderer renderer(markup);
        if (renderer.isValid()) {
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            renderer.render(&p, QRectF(0, 0, px * 2, px * 2));
            painted = true;
        }
    }
#endif
    if (!painted) {
        // Fallback без QtSvg: image-плагин qsvg (ставится вместе с Qt).
        QBuffer buf(const_cast<QByteArray*>(&markup));
        buf.open(QIODevice::ReadOnly);
        QImageReader r(&buf);
        if (QImage src = r.read(); !src.isNull()) {
            QPainter p(&img);
            p.setRenderHint(QPainter::SmoothPixmapTransform, true);
            p.drawImage(QRectF(0, 0, px * 2, px * 2), src);
            painted = true;
        }
    }
    if (!painted) {
        // Последний рубеж: нейтральная коробка кистью — карточка не должна
        // остаться пустой ни в какой сборке.
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        QLinearGradient g(0, 0, px, px * 2);
        g.setColorAt(0, QColor(0xC4, 0xA5, 0xFF));
        g.setColorAt(1, QColor(0x7C, 0x3A, 0xED));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRectF(px * 0.14, px * 0.4, px * 0.72, px * 0.44),
                          px * 0.1, px * 0.1);
        p.setBrush(QColor(0xA7, 0x8B, 0xFA));
        p.drawRoundedRect(QRectF(px * 0.09, px * 0.28, px * 0.82, px * 0.18),
                          px * 0.07, px * 0.07);
    }
    QPixmap pm = QPixmap::fromImage(img);
    pm.setDevicePixelRatio(2.0);
    QMutexLocker lock(&g_mutex);
    g_cache.insert(key, pm);
    return pm;
}

QString GiftArt::name(const QString& slug) {
    const int i = indexOf(slug);
    return i >= 0 ? QString::fromUtf8(GiftArtData::entries()[i].name) : QString();
}

QString GiftArt::rarityFor(const QString& slug, const QString& serverRarity) {
    static const struct { const char* server; const char* tier; } kUp[] = {
        {"limited", "epic"}, {"event", "rare"}};
    static const char* kTiers[] = {"common", "rare", "epic", "legendary"};
    static const int kRank[] = {1, 2, 3, 4};

    const int i = indexOf(slug);
    const QString base = i >= 0
        ? QLatin1String(GiftArtData::entries()[i].rarity)
        : QStringLiteral("common");
    QString up;
    for (const auto& u : kUp)
        if (serverRarity == QLatin1String(u.server)) up = QLatin1String(u.tier);
    int rb = 0, ru = 0;
    for (int t = 0; t < 4; ++t) {
        if (base == QLatin1String(kTiers[t])) rb = kRank[t];
        if (up == QLatin1String(kTiers[t])) ru = kRank[t];
    }
    for (int t = 3; t >= 0; --t)
        if (kRank[t] == qMax(rb, ru)) return QLatin1String(kTiers[t]);
    return QStringLiteral("common");
}

QString GiftArt::rarityLabel(const QString& tier) {
    if (tier == QLatin1String("rare")) return QString::fromUtf8("Редкий");
    if (tier == QLatin1String("epic")) return QString::fromUtf8("Эпический");
    if (tier == QLatin1String("legendary")) return QString::fromUtf8("Легендарный");
    return QString::fromUtf8("Обычный");
}

int GiftArt::number(const QString& instanceId) {
    if (instanceId.isEmpty()) return 0;
    const QByteArray s = instanceId.toUtf8();
    // Семантика number() из gift-art.js: h живёт как double; ^= даёт ЗНАКОВЫЙ
    // int32; произведение округляется double'ом (вылезает за 2^53), а >>> 0 —
    // ToUint32. Не «чистый» FNV-1a, но точная копия арифметики веба: номера
    // экземпляров обязаны совпадать на всех платформах.
    double h = 2166136261.0;   // 0x811c9dc5
    for (char c : s) {
        const qint32 hx = static_cast<qint32>(static_cast<qint64>(h))
                          ^ static_cast<qint32>(static_cast<unsigned char>(c));
        const double prod = static_cast<double>(hx) * 16777619.0;
        h = static_cast<quint32>(static_cast<qint64>(prod));   // truncate + mod 2^32
    }
    const quint32 hu = static_cast<quint32>(static_cast<qint64>(h));
    return static_cast<int>(hu % 90000u + 10000u);
}

