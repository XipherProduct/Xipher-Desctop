#include "ui/AvatarUtil.h"
#include "net/FileCache.h"
#include "net/Session.h"

#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>

namespace {
QNetworkAccessManager& nam() {
    static QNetworkAccessManager m;
    return m;
}
// Ключ «url@size»: список чатов и профиль просят один url в разных size.
QString cacheKey(const QString& url, int size) {
    return url + QLatin1Char('@') + QString::number(size);
}
QHash<QString, QPixmap>& cache() {
    static QHash<QString, QPixmap> c;
    return c;
}
const QString kBase = QStringLiteral("https://messenger.xipher.pro");

QPixmap roundFromImage(const QPixmap& src, int size) {
    const qreal dpr = 2.0;
    QPixmap out(int(size * dpr), int(size * dpr));
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, size, size);
    p.setClipPath(clip);
    QPixmap scaled = src.scaled(int(size * dpr), int(size * dpr),
                                Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawPixmap(0, 0, size, size, scaled);
    return out;
}
}

namespace Avatar {

QPixmap roundLetter(const QString& text, int size) {
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, QColor(0x8B, 0x5C, 0xF6));
    g.setColorAt(1, QColor(0x6D, 0x28, 0xD9));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(0, 0, size, size);

    QString letter = text.trimmed().left(1).toUpper();
    if (letter.isEmpty()) letter = QStringLiteral("?");
    QFont f = p.font();
    f.setPixelSize(int(size * 0.42));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0xFF, 0xFF, 0xFF));
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, letter);
    return pm;
}

void setRound(QLabel* label, const QString& url, const QString& fallbackText, int size) {
    if (!label) return;
    label->setFixedSize(size, size);
    label->setPixmap(roundLetter(fallbackText, size));   // плейсхолдер сразу

    if (url.isEmpty()) return;

    const QString ck = cacheKey(url, size);
    if (cache().contains(ck)) {
        label->setPixmap(cache().value(ck));
        return;
    }

    // Уже скачивали (в т.ч. в прошлых запусках) — мгновенно из локального
    // зашифрованного кэша, без сети.
    QByteArray avBytes;
    if (FileCache::instance().lookup(url, &avBytes)) {
        QPixmap src;
        if (src.loadFromData(avBytes)) {
            const QPixmap round = roundFromImage(src, size);
            cache().insert(ck, round);
            label->setPixmap(round);
            return;
        }
    }

    QString full = url.startsWith(QStringLiteral("http")) ? url : (kBase + url);
    QNetworkRequest req((QUrl(full)));
    req.setRawHeader("Authorization", "Bearer " + Session::instance().token.toUtf8());

    QPointer<QLabel> guard(label);
    const QString key = url;
    QNetworkReply* reply = nam().get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, guard, key, size]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        QPixmap src;
        if (!src.loadFromData(data)) return;
        FileCache::instance().store(key, data);   // повторно не качаем
        QPixmap round = roundFromImage(src, size);
        cache().insert(cacheKey(key, size), round);
        if (guard) guard->setPixmap(round);
    });
}

} // namespace Avatar

// ─── FolderIcons: набор 1:1 с веб-клиентом (chat.js FOLDER_ICON_KEYS/COLOR_SET) ───
namespace FolderIcons {

QString glyph(const QString& key) {
    static const QHash<QString, QString> glyphs = {
        {QStringLiteral("chat"), QStringLiteral("💬")},
        {QStringLiteral("group"), QStringLiteral("👥")},
        {QStringLiteral("channel"), QStringLiteral("📣")},
        {QStringLiteral("user"), QStringLiteral("👤")},
        {QStringLiteral("star"), QStringLiteral("⭐")},
        {QStringLiteral("heart"), QStringLiteral("❤️")},
        {QStringLiteral("fire"), QStringLiteral("🔥")},
        {QStringLiteral("bolt"), QStringLiteral("⚡")},
        {QStringLiteral("rocket"), QStringLiteral("🚀")},
        {QStringLiteral("lightbulb"), QStringLiteral("💡")},
        {QStringLiteral("bookmark"), QStringLiteral("🔖")},
        {QStringLiteral("crown"), QStringLiteral("👑")},
        {QStringLiteral("business"), QStringLiteral("💼")},
        {QStringLiteral("education"), QStringLiteral("🎓")},
        {QStringLiteral("tech"), QStringLiteral("💻")},
        {QStringLiteral("crypto"), QStringLiteral("🪙")},
        {QStringLiteral("gaming"), QStringLiteral("🎮")},
        {QStringLiteral("music"), QStringLiteral("🎵")},
        {QStringLiteral("movies"), QStringLiteral("🎬")},
        {QStringLiteral("art"), QStringLiteral("🎨")},
        {QStringLiteral("sport"), QStringLiteral("⚽")},
        {QStringLiteral("food"), QStringLiteral("🍔")},
        {QStringLiteral("travel"), QStringLiteral("✈️")},
        {QStringLiteral("health"), QStringLiteral("💊")},
        {QStringLiteral("science"), QStringLiteral("🔬")},
        {QStringLiteral("news"), QStringLiteral("📰")},
        {QStringLiteral("globe"), QStringLiteral("🌐")},
        {QStringLiteral("phone"), QStringLiteral("📞")},
        {QStringLiteral("party"), QStringLiteral("🎉")},
        {QStringLiteral("wrench"), QStringLiteral("🔧")},
        {QStringLiteral("description"), QStringLiteral("📄")},
        {QStringLiteral("notification"), QStringLiteral("🔔")},
    };
    return glyphs.value(key, QStringLiteral("💬"));
}

QStringList keys() {
    return {
        QStringLiteral("chat"), QStringLiteral("group"), QStringLiteral("channel"),
        QStringLiteral("user"), QStringLiteral("star"), QStringLiteral("heart"),
        QStringLiteral("fire"), QStringLiteral("bolt"), QStringLiteral("rocket"),
        QStringLiteral("lightbulb"), QStringLiteral("bookmark"), QStringLiteral("crown"),
        QStringLiteral("business"), QStringLiteral("education"), QStringLiteral("tech"),
        QStringLiteral("crypto"), QStringLiteral("gaming"), QStringLiteral("music"),
        QStringLiteral("movies"), QStringLiteral("art"), QStringLiteral("sport"),
        QStringLiteral("food"), QStringLiteral("travel"), QStringLiteral("health"),
        QStringLiteral("science"), QStringLiteral("news"), QStringLiteral("globe"),
        QStringLiteral("phone"), QStringLiteral("party"), QStringLiteral("wrench"),
        QStringLiteral("description"), QStringLiteral("notification"),
    };
}

QStringList colors() {
    return {
        QStringLiteral("#6fb1fc"), QStringLiteral("#ffa44e"), QStringLiteral("#ff7a82"),
        QStringLiteral("#b691ff"), QStringLiteral("#ffbc5c"), QStringLiteral("#7ed7a8"),
        QStringLiteral("#ff87b0"), QStringLiteral("#8fd3ff"),
    };
}

} // namespace FolderIcons
