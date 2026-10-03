#include "ui/ProfilePanel.h"
#include "ui/AvatarUtil.h"
#include "ui/GiftArt.h"
#include "ui/Icons.h"
#include "ui/ModalOverlay.h"
#include "ui/SuperSearchDialog.h"   // SuperSearchClickFilter (клики по строкам)
#include "net/ApiClient.h"
#include "net/FileCache.h"
#include "net/Prefs.h"
#include "net/Session.h"
#include "util/QrCode.h"

#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QJsonValue>
#include <QJsonArray>
#include <QScrollArea>
#include <QStackedLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMenu>
#include <QClipboard>
#include <QApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

// ─────────────────────────────────────────────────────────────────────────────
//  Порт js/profile/view.js + css/profile.css веба (Профиль v2).
//  Палитра — токены tokens.css; размеры — из profile.css, без «на глаз».
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Токены темы (тёмная), 1:1 с tokens.css.
const QColor  kSurf1(0x13, 0x12, 0x18);      // surface-1 — фон панели
const QColor  kSurf2(0x1A, 0x18, 0x22);      // surface-2 — секции
const QColor  kSurf3(0x22, 0x1F, 0x2C);      // surface-3 — чипы иконок, hover
const QColor  kText1(0xF3, 0xF1, 0xF8);      // text-primary
const QColor  kText2(0xAC, 0xA6, 0xBD);      // text-secondary
const QColor  kText3(0x72, 0x6C, 0x82);      // text-tertiary
const QColor  kAccent(0x8B, 0x5C, 0xF6);     // accent
const QColor  kAccentDeep(0x6D, 0x28, 0xD9); // accent-deep
const QColor  kAccentText(0xBB, 0xA4, 0xFF); // accent-text
const QColor  kSuccess(0x46, 0xB9, 0x8A);    // success («в сети»)
const QColor  kDanger(0xE2, 0x6A, 0x63);     // danger
const QColor  kRowIcon(0xB9, 0xB4, 0xC7);    // иконки строк сведений
const QColor  kLinkAccent(0x8C, 0x7B, 0xFF); // строка канала
const int     kBannerH   = 186;              // --xp-banner-h
const int     kAvatarSz  = 96;               // .xp-head__avatar

const char* kMonths[] = {"", "января","февраля","марта","апреля","мая","июня",
                         "июля","августа","сентября","октября","ноября","декабря"};

// ── Разбор времени PostgreSQL (parseServerTime веба) ────────────────────────
// timestamptz::text даёт «2026-03-04 15:01:00.872664+00»: пробел вместо T и
// ДВУЗНАЧНОЕ смещение. Без нормализации строка не разбирается вовсе.
QDateTime parseServerTime(const QString& raw) {
    if (raw.isEmpty()) return QDateTime();
    QString s = raw.trimmed();
    s.replace(QLatin1Char(' '), QLatin1Char('T'));
    static const QRegularExpression twoDigitOffset(QStringLiteral("[+-]\\d{2}$"));
    if (!s.endsWith(QLatin1Char('Z')) && !s.endsWith(QLatin1Char('z'))
        && twoDigitOffset.match(s).hasMatch()) {
        s += QStringLiteral(":00");
    }
    QDateTime d = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!d.isValid()) d = QDateTime::fromString(s, Qt::ISODate);
    if (!d.isValid()) {
        d = QDateTime::fromString(s.left(19), QStringLiteral("yyyy-MM-ddThh:mm:ss"));
        d.setTimeSpec(Qt::UTC);
    }
    return d;
}

// «был(а) в сети …» — 1:1 с lastSeenText веба: точное время только сегодня/вчера.
QString lastSeenText(const QString& iso, bool online) {
    if (online) return QStringLiteral("в сети");
    if (iso.isEmpty()) return QString();
    const QDateTime d = parseServerTime(iso);
    if (!d.isValid()) return QString();
    const QDateTime now = QDateTime::currentDateTime();
    const qint64 diffMin = d.secsTo(now) / 60;
    if (diffMin < 1) return QStringLiteral("был(а) только что");
    if (diffMin < 60) return QStringLiteral("был(а) %1 мин назад").arg(diffMin);
    const QString hhmm = d.time().toString(QStringLiteral("HH:mm"));
    if (d.date() == now.date()) return QStringLiteral("был(а) в ") + hhmm;
    if (d.date().daysTo(now.date()) == 1) return QStringLiteral("был(а) вчера в ") + hhmm;
    return QStringLiteral("был(а) %1 %2").arg(d.date().day())
                                         .arg(QString::fromUtf8(kMonths[d.date().month()]));
}

// «22 января 2008 (18)» — birthdayText веба (возраст честно до дня рождения).
QString birthdayText(const QJsonObject& p) {
    const int day = p.value(QStringLiteral("birth_day")).toInt(0);
    const int month = p.value(QStringLiteral("birth_month")).toInt(0);
    if (day <= 0 || month <= 0) return QString();
    QString s = QStringLiteral("%1 %2").arg(day).arg(QString::fromUtf8(kMonths[month]));
    const int year = p.value(QStringLiteral("birth_year")).toInt(0);
    if (year > 0) {
        const QDate today = QDate::currentDate();
        int age = today.year() - year;
        if (today.month() < month || (today.month() == month && today.day() < day)) --age;
        s += QStringLiteral(" %1 (%2)").arg(year).arg(age);
    }
    return s;
}

// «6 декабря 2025» — joinedText веба: год всегда.
QString joinedText(const QJsonObject& p) {
    const QDateTime d = parseServerTime(p.value(QStringLiteral("created_at")).toString());
    if (!d.isValid()) return QString();
    return QStringLiteral("%1 %2 %3").arg(d.date().day())
        .arg(QString::fromUtf8(kMonths[d.date().month()])).arg(d.date().year());
}

// Инициалы для аватара-заглушки (initials веба): две буквы, не одна.
QString initials(const QString& name) {
    const QStringList parts = name.trimmed().split(QRegularExpression(QStringLiteral("\\s+")));
    QString s;
    if (!parts.isEmpty() && !parts.first().isEmpty()) s += parts.first().left(1);
    if (parts.size() > 1 && !parts.at(1).isEmpty()) s += parts.at(1).left(1);
    if (s.isEmpty()) s = QStringLiteral("?");
    return s.toUpper();
}

QPixmap fallbackAvatar(const QString& name, int size) {
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QLinearGradient g(0, 0, size, size);
    g.setColorAt(0, kAccent);
    g.setColorAt(1, kAccentDeep);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(0, 0, size, size);
    QFont f = p.font();
    f.setPixelSize(int(size * 0.36));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0xFF, 0xFF, 0xFF));
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, initials(name));
    p.end();
    return pm;
}

QPixmap coverFromImage(const QPixmap& src, int w, int h) {
    const qreal dpr = 2.0;
    QPixmap out(int(w * dpr), int(h * dpr));
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal scale = qMax(qreal(w) / src.width(), qreal(h) / src.height());
    const int sw = qRound(w / scale), sh = qRound(h / scale);
    const int sx = (src.width() - sw) / 2, sy = (src.height() - sh) / 2;
    p.drawPixmap(0, 0, w, h, src.copy(sx, sy, sw, sh));
    p.end();
    return out;
}

// Загрузка обложки: дисковый кэш → сеть (как Avatar, но прямоугольная).
void loadCover(const QString& url, int w, int h, const std::function<void(QPixmap)>& cb) {
    static QNetworkAccessManager nam;
    static QHash<QString, QPixmap> cache;
    if (url.isEmpty()) return;
    if (cache.contains(url)) { cb(cache.value(url)); return; }
    QByteArray bytes;
    if (FileCache::instance().lookup(url, &bytes)) {
        QPixmap src;
        if (src.loadFromData(bytes)) {
            const QPixmap cover = coverFromImage(src, w, h);
            cache.insert(url, cover);
            cb(cover);
            return;
        }
    }
    const QString full = url.startsWith(QStringLiteral("http"))
        ? url : (QStringLiteral("https://messenger.xipher.pro") + url);
    QNetworkRequest req((QUrl(full)));
    req.setRawHeader("Authorization", "Bearer " + Session::instance().token.toUtf8());
    QNetworkReply* reply = nam.get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, url, w, h, cb]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) return;
        const QByteArray data = reply->readAll();
        QPixmap src;
        if (!src.loadFromData(data)) return;
        FileCache::instance().store(url, data);
        cb(coverFromImage(src, w, h));
    });
}

QString mutedKeyFor(const QString& userId) {
    return QStringLiteral("chat:") + userId;
}
bool isChatMuted(const QString& userId) {
    return Prefs::getStr(QStringLiteral("xipher_muted_chats")).contains(mutedKeyFor(userId));
}
void setChatMuted(const QString& userId, bool muted) {
    QString cur = Prefs::getStr(QStringLiteral("xipher_muted_chats"));
    const QString key = mutedKeyFor(userId);
    if (muted && !cur.contains(key)) {
        if (!cur.isEmpty()) cur += QLatin1Char(',');
        cur += key;
        Prefs::setStr(QStringLiteral("xipher_muted_chats"), cur);
    } else if (!muted) {
        cur.replace(key + QStringLiteral(","), QString())
           .replace(QStringLiteral(",") + key, QString())
           .replace(key, QString());
        Prefs::setStr(QStringLiteral("xipher_muted_chats"), cur);
    }
}

// Кэш ответов на 60 секунд (как CACHE_MS в view.js): профиль одного и того
// же человека открывают по нескольку раз подряд.
constexpr int kCacheMs = 60000;
QHash<QString, QPair<qint64, QJsonObject>>& profileCache() {
    static QHash<QString, QPair<qint64, QJsonObject>> c;
    return c;
}

const char* kPanelQss = R"QSS(
/* Окно 560px по центру: #131218, радиус 24, тонкая рамка (profile.css). */
#modalCard { background:#131218; border:1px solid rgba(255,255,255,0.055); border-radius:24px; }
QScrollArea { background:transparent; border:none; }
/* Скроллбар-оверлей: в покое невидим, ползунок проявляется при наведении —
   постоянная серая полоса читалась как «линия» поверх профиля. */
QScrollBar:vertical { background:transparent; width:8px; margin:2px; }
QScrollBar::handle:vertical { background:transparent; border-radius:4px; min-height:36px; }
QScrollBar::handle:vertical:hover { background:rgba(255,255,255,0.18); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }

/* Кнопка-крестик на обложке: полупрозрачный чёрный круг (на баннере). */
#profClose { background:rgba(0,0,0,0.38); border:none; border-radius:14px;
             color:#FFFFFF; font-size:17px; font-weight:600; }
#profClose:hover { background:rgba(0,0,0,0.55); }

/* Действия: тёмная плитка 64px, иконка + подпись (.xp-act). */
#profAct { background:#1A1822; border:1px solid rgba(255,255,255,0.055);
           border-radius:14px; }
#profAct:hover:enabled { background:#221F2C; }

/* Секции (.xp-sec) и их заголовки. */
#profSec { background:#1A1822; border-radius:20px; }
#profSecTitle { color:#726C82; font-size:12px; font-weight:600;
                letter-spacing:0.6px; text-transform:uppercase; }
#profSecCount { color:#ACA6BD; font-size:13px; }

/* Строки сведений (.xp-row): иконка-чип 34px + значение/лейбл. */
#profRowValue { color:#F3F1F8; font-size:15px; }
#profRowLabel { color:#726C82; font-size:12px; }
#profLinkBtn, #profMoreBtn { background:transparent; border:none; border-radius:12px;
    color:#BBA4FF; font-size:14px; padding:12px; }
#profLinkBtn:hover, #profMoreBtn:hover { background:#221F2C; }

/* Строка канала (.xp-row--link). */
#profChanValue { color:#8C7BFF; font-size:15px; font-weight:600; }

/* Подарки (.xp-gift) и знаки (.xp-mark). */
#profGiftName { color:#ACA6BD; font-size:11px; }
#profMarkTitle { color:#F3F1F8; font-size:14px; }
#profMarkMeta { color:#726C82; font-size:12px; }

/* Нижние действия (.xp-bottom). */
#profBottomItem { background:transparent; border:none; border-radius:12px;
    color:#F3F1F8; font-size:15px; text-align:left; padding:12px 16px; }
#profBottomItem:hover { background:#221F2C; }
#profBottomItemDanger { background:transparent; border:none; border-radius:12px;
    color:#E26A63; font-size:15px; text-align:left; padding:12px 16px; }
#profBottomItemDanger:hover { background:rgba(226,106,99,0.14); }

/* Ошибка / QR / подарки-диалог. */
#profErrorTitle { color:#F3F1F8; font-size:17px; font-weight:600; }
#profErrorText { color:#ACA6BD; font-size:14px; }
#primaryBtn { background:#8B5CF6; color:#FFFFFF; border:none; border-radius:999px;
              padding:12px 24px; font-size:14px; }
#primaryBtn:hover { background:#9B72F8; }
QLineEdit { background:#100F15; border:1px solid rgba(255,255,255,0.10);
            border-radius:12px; min-height:38px; padding:0 12px; color:#F3F1F8; }
QLineEdit:focus { border-color:rgba(139,92,246,0.34); }
#giftCard { background:#1A1822; border:1px solid #2B2737; border-radius:20px; }
#giftCard:hover { background:#221F2C; border-color:#8B5CF6; }
QMenu { background:#221F2C; border:1px solid rgba(255,255,255,0.10); border-radius:14px;
        padding:4px; color:#F3F1F8; }
QMenu::item { padding:8px 12px; border-radius:12px; }
QMenu::item:selected { background:#2B2737; }
QMenu::separator { height:1px; background:rgba(255,255,255,0.08); margin:4px 8px; }
)QSS";

// ── Знак идентичности (js/profile/signet.js): РОВНО ОДИН значок у имени. ───
class SignetBadge : public QWidget {
public:
    SignetBadge(const QJsonObject& signet, int size, QWidget* parent = nullptr)
        : QWidget(parent), size_(size),
          frame_(signet.value(QStringLiteral("frame")).toString()),
          core_(signet.value(QStringLiteral("core")).toString()) {
        setFixedSize(size, size);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.scale(size_ / 24.0, size_ / 24.0);
        // Оправа — класс доверия: щит (официальный/бот) или рамка-квадрат
        // (бизнес). Цвет оправы задаёт только сервер.
        QColor col = frame_ == QLatin1String("official") ? QColor(0x4F, 0xC5, 0xA6)
                   : frame_ == QLatin1String("system_bot") ? kAccent
                   : frame_ == QLatin1String("business")   ? QColor(0xD9, 0xA0, 0x5B)
                   : kAccentText;
        if (frame_ == QLatin1String("business")) {
            QPainterPath sq;
            sq.setFillRule(Qt::OddEvenFill);
            sq.addRoundedRect(QRectF(3.4, 3.4, 17.2, 17.2), 2.8, 2.8);
            sq.addRoundedRect(QRectF(6.6, 6.6, 10.8, 10.8), 0.8, 0.8);
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawPath(sq);
        } else {
            QPainterPath sh;
            sh.moveTo(12, 2.2);
            sh.lineTo(4.6, 5); sh.lineTo(4.6, 11.1);
            sh.cubicTo(4.6, 15.7, 7.7, 19.4, 12, 20.8);
            sh.cubicTo(16.3, 19.4, 19.4, 15.7, 19.4, 11.1);
            sh.lineTo(19.4, 5);
            sh.closeSubpath();
            p.setPen(QPen(col, 1.7));
            p.setBrush(Qt::NoBrush);
            p.drawPath(sh);
        }
        // Ядро — одна отметка на выбор пользователя; формы различимы силуэтом.
        QPainterPath core;
        if (core_ == QLatin1String("founder")) {
            const QPointF pts[] = {{12,6.4},{13.7,9.9},{17.6,10.5},{14.8,13.2},
                {15.5,17.0},{12,15.2},{8.5,17.0},{9.2,13.2},{6.4,10.5},{10.3,9.9}};
            core.moveTo(pts[0]);
            for (const QPointF* pt = pts + 1; pt != pts + 10; ++pt) core.lineTo(*pt);
            core.closeSubpath();
        } else if (core_ == QLatin1String("contrib")) {
            core.addPolygon(QPolygonF({
                QPointF(10.6,7.4), QPointF(13.4,7.4), QPointF(13.4,10.6),
                QPointF(16.6,10.6), QPointF(16.6,13.4), QPointF(13.4,13.4),
                QPointF(13.4,16.6), QPointF(10.6,16.6), QPointF(10.6,13.4),
                QPointF(7.4,13.4), QPointF(7.4,10.6), QPointF(10.6,10.6)}));
            core.closeSubpath();
        } else if (core_ == QLatin1String("veteran")) {
            core.addPolygon(QPolygonF({QPointF(12,6.4), QPointF(16.6,9.1),
                QPointF(16.6,14.9), QPointF(12,17.6), QPointF(7.4,14.9),
                QPointF(7.4,9.1)}));
            core.closeSubpath();
        } else if (core_ == QLatin1String("bug")) {
            core.addEllipse(QPointF(11, 11), 3.6, 3.6);
        } else if (core_ == QLatin1String("tester")) {
            core.addPolygon(QPolygonF({QPointF(9.2,7), QPointF(14.8,7),
                QPointF(14.8,9.1), QPointF(13.2,11), QPointF(15.6,15.2),
                QPointF(15.5,16.8), QPointF(8.5,16.8), QPointF(8.4,15.2),
                QPointF(10.8,11), QPointF(9.2,9.1)}));
            core.closeSubpath();
        } else if (!core_.isEmpty()) {
            core.addEllipse(QPointF(12, 12), 1.6, 1.6);
        }
        if (!core_.isEmpty()) {
            p.setPen(Qt::NoPen);
            p.setBrush(col);
            p.drawPath(core);
        }
    }

private:
    int size_;
    QString frame_, core_;
};

// ── Кольцо аватара: тёмный круг 102px, внутри фото 96px ─────────────────────
class AvatarRing : public QWidget {
public:
    explicit AvatarRing(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(kAvatarSz + 6, kAvatarSz + 6);
        pic_ = new QLabel(this);
        pic_->setGeometry(3, 3, kAvatarSz, kAvatarSz);
        pic_->setAlignment(Qt::AlignCenter);
        setStyleSheet(QStringLiteral("background:transparent;"));
    }
    QLabel* picture() const { return pic_; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(kSurf1);
        p.drawEllipse(rect());
    }
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        pic_->setGeometry(3, 3, kAvatarSz, kAvatarSz);
    }
private:
    QLabel* pic_;
};

// ── Шапка: баннер 186px с затуханием вниз, аватар внахлёст наполовину ──────
class HeadWidget : public QWidget {
public:
    HeadWidget(const QJsonObject& p, const QString& knownAvatar, QWidget* parent = nullptr)
        : QWidget(parent),
          personalColor_(p.value(QStringLiteral("personal_color")).toString()),
          bannerUrl_(p.value(QStringLiteral("banner_url")).toString()) {
        setStyleSheet(QStringLiteral("background:transparent;"));

        const QString displayName = p.value(QStringLiteral("display_name")).toString(
            p.value(QStringLiteral("username")).toString(QStringLiteral("?")));
        hasBannerImage_ = !bannerUrl_.isEmpty();

        // Кольцо аватара: круг рисуется QPainter'ом (102 = фото 96 + рамка 3),
        // фото — QLabel ровно 96 внутри. setRound ресайзит только внутренний
        // лейбл, геометрия не зависит от порядка загрузки.
        avatar_ = new AvatarRing(this);
        avatarPic_ = avatar_->picture();
        const QString avUrl = p.value(QStringLiteral("avatar_url")).toString(knownAvatar);
        if (avUrl.isEmpty()) avatarPic_->setPixmap(fallbackAvatar(displayName, kAvatarSz));
        else Avatar::setRound(avatarPic_, avUrl, displayName, kAvatarSz);

        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(16, avatarY() + kAvatarSz + 6 + 12, 16, 16);
        lay->setSpacing(0);

        auto* nameRow = new QHBoxLayout();
        nameRow->setSpacing(6);
        nameRow->addStretch(1);
        auto* name = new QLabel(displayName, this);
        name->setStyleSheet(QStringLiteral(
            "color:#F3F1F8;font-size:21px;font-weight:700;background:transparent;"));
        name->setWordWrap(true);
        name->setAlignment(Qt::AlignHCenter);
        nameRow->addWidget(name, 0, Qt::AlignVCenter);
        const QJsonObject signet = p.value(QStringLiteral("signet")).toObject();
        if (!signet.isEmpty())
            nameRow->addWidget(new SignetBadge(signet, 20, this), 0, Qt::AlignBottom);
        nameRow->addStretch(1);
        lay->addLayout(nameRow);

        const QString uname = p.value(QStringLiteral("username")).toString();
        if (!uname.isEmpty()) {
            auto* un = new QLabel(QStringLiteral("@") + uname, this);
            un->setStyleSheet(QStringLiteral(
                "color:#726C82;font-size:14px;background:transparent;"));
            un->setAlignment(Qt::AlignHCenter);
            un->setTextInteractionFlags(Qt::TextSelectableByMouse);
            lay->addSpacing(2);
            lay->addWidget(un);
        }

        QString sub = lastSeenText(p.value(QStringLiteral("last_seen")).toString(),
                                   p.value(QStringLiteral("is_online")).toBool(false));
        const QString at = p.value(QStringLiteral("account_type")).toString();
        if (at == QLatin1String("deleted")) sub = QStringLiteral("Удалённый аккаунт");
        else if (at == QLatin1String("official_bot") || at == QLatin1String("bot"))
            sub = QStringLiteral("Бот");
        if (!sub.isEmpty()) {
            auto* st = new QLabel(sub, this);
            st->setStyleSheet(QStringLiteral("color:%1;font-size:14px;background:transparent;")
                .arg(p.value(QStringLiteral("is_online")).toBool(false)
                         ? QStringLiteral("#46B98A") : QStringLiteral("#ACA6BD")));
            st->setAlignment(Qt::AlignHCenter);
            lay->addSpacing(4);
            lay->addWidget(st);
        }

        // Пользовательский статус — отдельной строкой-пилюлей.
        const QString moodEmoji = p.value(QStringLiteral("status_emoji")).toString();
        const QString moodText = p.value(QStringLiteral("status_text")).toString();
        if (!moodEmoji.isEmpty() || !moodText.isEmpty()) {
            auto* mood = new QLabel(
                (moodEmoji.isEmpty() ? QString() : moodEmoji + QLatin1Char(' ')) + moodText, this);
            mood->setStyleSheet(QStringLiteral(
                "color:#ACA6BD;font-size:13px;background:#1A1822;border-radius:12px;"
                "padding:5px 12px;"));
            mood->setAlignment(Qt::AlignHCenter);
            lay->addSpacing(8);
            lay->addWidget(mood, 0, Qt::AlignHCenter);
        }

        if (hasBannerImage_) {
            loadCover(bannerUrl_, 560, kBannerH, [this](QPixmap pm) {
                bannerPm_ = pm;
                update();
            });
        }
    }

    int avatarY() const { return hasBannerImage_ ? kBannerH - 51 : 76; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF banner(0, 0, width(), kBannerH);

        // Скруглённые ВЕРХНИЕ углы баннера под радиус карточки (24):
        // QSS border-radius карточки не клипит детей, и без этого баннер
        // вылезает прямыми углами за скругление (overflow:hidden в вебе).
        QPainterPath round;
        round.setFillRule(Qt::WindingFill);   // OddEven выкусил бы перекрытие
        round.addRoundedRect(banner, 24, 24);
        round.addRect(0, 24, width(), kBannerH);
        p.save();
        p.setClipPath(round);

        // Градиент бренда, либо personal_color → transparent (135°).
        QLinearGradient bg(0, 0, kBannerH, kBannerH);
        if (personalColor_.isValid()) {
            bg.setColorAt(0, personalColor_);
            bg.setColorAt(1, QColor(personalColor_.red(), personalColor_.green(),
                                    personalColor_.blue(), 0));
        } else {
            bg.setColorAt(0, kAccent);
            bg.setColorAt(1, kAccentDeep);
        }
        p.fillRect(banner, bg);
        if (!bannerPm_.isNull()) p.drawPixmap(banner.toRect(), bannerPm_);

        // Затухание вниз (.xp-head__banner::after): имя читается на любом фоне.
        QLinearGradient fade(0, 0, 0, kBannerH);
        fade.setColorAt(0.0, QColor(0, 0, 0, 38));
        fade.setColorAt(0.6, QColor(0, 0, 0, 89));
        fade.setColorAt(1.0, kSurf1);
        p.fillRect(banner, fade);
        p.restore();
    }
    void resizeEvent(QResizeEvent* e) override {
        QWidget::resizeEvent(e);
        avatar_->move((width() - avatar_->width()) / 2, avatarY());
    }

private:
    QColor  personalColor_;    // личный цвет владельца (может быть невалиден)
    QString bannerUrl_;
    QPixmap bannerPm_;
    bool hasBannerImage_ = false;
    AvatarRing* avatar_ = nullptr;  // кольцо 102px (круг)
    QLabel*     avatarPic_ = nullptr; // фото 96px внутри кольца
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Скелетон — точная геометрия .xp-skel: баннер 132, аватар внахлёст -48,
//  строки 55%/35%, четыре плитки действий 64px, блок 120. Shimmer — полоса
//  света от акцента темы, 1.4с, зациклена (xp-shimmer).
// ─────────────────────────────────────────────────────────────────────────────
ProfileSkeleton::ProfileSkeleton(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(450);
    shimmer_ = new QVariantAnimation(this);
    shimmer_->setStartValue(0.0);
    shimmer_->setEndValue(1.0);
    shimmer_->setDuration(1400);
    shimmer_->setLoopCount(-1);
    shimmer_->setEasingCurve(QEasingCurve::InOutSine);
    connect(shimmer_, &QVariantAnimation::valueChanged, this, [this]() { update(); });
    shimmer_->start();
}

ProfileSkeleton::~ProfileSkeleton() = default;

void ProfileSkeleton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const int W = width();
    const qreal t = shimmer_->currentValue().toReal();

    struct Part { QRectF r; qreal radius; QColor base; bool circle; };
    const int actY = 254, actH = 64, pad = 16, gap = 8;
    const int actW = (W - pad * 2 - gap * 3) / 4;
    QList<Part> parts;
    parts << Part{QRectF(0, 0, W, 132), 0, kSurf2, false};                    // баннер
    parts << Part{QRectF(W / 2.0 - 48, 84, 96, 96), 48, kSurf3, true};        // аватар
    parts << Part{QRectF(W * 0.225, 192, W * 0.55, 20), 7, kSurf2, false};    // имя
    parts << Part{QRectF(W * 0.325, 224, W * 0.35, 14), 7, kSurf2, false};    // подпись
    for (int i = 0; i < 4; ++i)
        parts << Part{QRectF(pad + i * (actW + gap), actY, actW, actH), 14, kSurf2, false};
    parts << Part{QRectF(pad, 330, W - pad * 2, 120), 20, kSurf2, false};     // блок

    for (const Part& part : parts) {
        QPainterPath shape;
        if (part.circle) shape.addEllipse(part.r);
        else if (part.radius > 0) shape.addRoundedRect(part.r, part.radius, part.radius);
        else shape.addRect(part.r);
        p.fillPath(shape, part.base);

        // Световая полоса бежит слева направо по каждой плашке.
        p.save();
        p.setClipPath(shape);
        const qreal band = part.r.width() * 0.55;
        const qreal x0 = part.r.left() - band + t * (part.r.width() + band * 2);
        QLinearGradient g(x0, 0, x0 + band, 0);
        g.setColorAt(0.0, QColor(139, 92, 246, 0));
        g.setColorAt(0.42, QColor(139, 92, 246, 36));
        g.setColorAt(0.5, QColor(255, 255, 255, 20));
        g.setColorAt(0.58, QColor(139, 92, 246, 36));
        g.setColorAt(1.0, QColor(139, 92, 246, 0));
        p.fillRect(part.r, g);
        p.restore();
    }

    // Кольцо аватара поверх шиммера.
    p.setPen(QPen(kSurf1, 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QRectF(W / 2.0 - 48, 84, 96, 96));
}

// ─────────────────────────────────────────────────────────────────────────────
//  ProfilePanel
// ─────────────────────────────────────────────────────────────────────────────
ProfilePanel::ProfilePanel(ApiClient* api, QWidget* parent)
    : ModalOverlay(parent, 560), api_(api) {
    card()->setStyleSheet(QLatin1String(kPanelQss));
    // Высота — по содержимому (fit-content в вебе): ручной минимум ломает
    // авторазмер, минимум держит сам скелетон/контент.
    card()->setMinimumHeight(0);
    enableAutoHeight();

    auto* outer = cardLayout();
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    stack_ = new QStackedLayout();
    outer->addLayout(stack_);

    scroll_ = new QScrollArea();
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet(QLatin1String(kPanelQss));
    auto* content = new QWidget();
    content->setStyleSheet(QStringLiteral("background:transparent;"));
    col_ = new QVBoxLayout(content);
    col_->setContentsMargins(0, 0, 0, 0);
    col_->setSpacing(0);
    col_->addStretch(1);
    scroll_->setWidget(content);
    stack_->addWidget(scroll_);

    // Кнопка-крестик поверх баннера (правый верхний угол).
    closeBtn_ = new QPushButton(QStringLiteral("✕"), card());
    closeBtn_->setObjectName(QStringLiteral("profClose"));
    closeBtn_->setCursor(Qt::PointingHandCursor);
    closeBtn_->setFixedSize(40, 40);
    connect(closeBtn_, &QPushButton::clicked, this, &ModalOverlay::closeAnimated);
    closeBtn_->raise();
    card()->installEventFilter(this);

    connect(api_, &ApiClient::profileViewLoaded, this,
            [this](qint64 reqId, const QJsonObject& data, bool ok, const QString& err) {
        applyProfileView(reqId, data, ok, err);
    });
    connect(api_, &ApiClient::mediaCountLoaded, this,
            [this](const QString& chatId, int total) { applyMediaCount(chatId, total); });
    // Коллекция подарков: для инлайн-фолбэка (ничего не закреплено) и для
    // экрана «Показать все подарки» (как openCollection веба).
    connect(api_, &ApiClient::userGiftsLoaded, this,
            [this](const QString& userId, const QJsonArray& gifts, bool ok, bool hidden) {
        Q_UNUSED(userId);
        if (userId != userId_) return;
        allGifts_ = gifts;
        allGiftsHidden_ = hidden || !ok;
        if (giftsInlinePending_) {
            giftsInlinePending_ = false;
            if (!allGiftsHidden_ && !allGifts_.isEmpty()) fillGiftsRow(allGifts_);
        }
        // Экран «Показать все подарки» уже открыт и ждал данных.
        if (collectionScreen_) {
            buildCollectionScreen(collectionScreen_);
            collectionScreen_ = nullptr;
        }
    });
}

bool ProfilePanel::eventFilter(QObject* obj, QEvent* e) {
    if (obj == card() && e->type() == QEvent::Resize)
        closeBtn_->move(card()->width() - closeBtn_->width() - 8, 8);
    return ModalOverlay::eventFilter(obj, e);
}

void ProfilePanel::setKnownPreview(const QString& name, const QString& avatarUrl, bool online) {
    knownName_ = name;
    knownAvatar_ = avatarUrl;
    knownOnline_ = online;
    hasPreview_ = !name.isEmpty();
}

void ProfilePanel::openFor(const QString& userId) {
    userId_ = userId;
    mediaTotal_ = -1;
    mediaRow_ = nullptr;
    lastData_ = QJsonObject();
    showAnimated();

    // Свежий кэш → рисуем сразу; иначе превью из списка чатов (шапка без
    // действий), а полный ответ приедет и перерисует (load() в view.js).
    const QString key = QStringLiteral("id:") + userId;
    const auto hit = profileCache().value(key);
    if (hit.first > 0 && QDateTime::currentMSecsSinceEpoch() - hit.first < kCacheMs) {
        renderProfile(hit.second);
    } else if (hasPreview_) {
        QJsonObject p;
        p.insert(QStringLiteral("display_name"), knownName_);
        p.insert(QStringLiteral("is_online"), knownOnline_);
        if (!knownAvatar_.isEmpty()) p.insert(QStringLiteral("avatar_url"), knownAvatar_);
        QJsonObject data;
        data.insert(QStringLiteral("success"), true);
        data.insert(QStringLiteral("profile"), p);
        data.insert(QStringLiteral("relation"), QJsonObject());
        data.insert(QStringLiteral("is_preview"), true);
        renderProfile(data);
    } else {
        renderSkeleton();
    }

    api_->profileView(userId);
    reqId_ = api_->profileViewReply();   // id ТОЛЬКО ЧТО отправленного запроса
    // Счётчик медиа уходит РАЗОМ с профилем (fetchMediaCount веба).
    api_->mediaCount(userId);
}

void ProfilePanel::applyProfileView(qint64 reqId, const QJsonObject& data,
                                    bool ok, const QString& error) {
    // Устаревший ответ (успели открыть другого человека) — мимо.
    if (reqId < reqId_) return;
    if (!ok && data.isEmpty()) {
        // Сеть молчит: если шапка уже показана (кэш/превью) — оставляем её.
        if (!lastData_.isEmpty()) return;
        renderError(error.isEmpty() ? QStringLiteral("Нет связи с сервером.") : error);
        return;
    }
    if (!data.value(QStringLiteral("success")).toBool(false)) {
        renderError(data.value(QStringLiteral("message")).toString(
            QStringLiteral("Проверьте соединение.")));
        return;
    }
    profileCache().insert(QStringLiteral("id:") + userId_,
        { QDateTime::currentMSecsSinceEpoch(), data });
    renderProfile(data);
}

void ProfilePanel::applyMediaCount(const QString& chatId, int total) {
    if (chatId != userId_) return;
    mediaTotal_ = total;
    if (!mediaRow_) return;
    mediaRow_->setVisible(total > 0);
    if (auto* cnt = mediaRow_->findChild<QLabel*>(QStringLiteral("profMediaCount")))
        cnt->setText(QString::number(total));
}

void ProfilePanel::clearContent() {
    while (col_->count() > 1) {
        QLayoutItem* it = col_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
}

// Карточка растёт под контент (height:fit-content веба), но не выше 92%
// окна; излишек уезжает в прокрутку области.
void ProfilePanel::fitHeight() {
    QWidget* c = scroll_->widget();
    if (!c || !c->layout()) return;
    // Дети, созданные уже после show() панели, могут нести скрытый флаг —
    // layout считает их «пустыми» и карточка получает ленивые ~400px вместо
    // полной высоты контента. Показываем явно и форсируем расчёт.
    const int n = c->layout()->count();
    int want = 4;
    for (int i = 0; i < n; ++i) {
        QLayoutItem* it = c->layout()->itemAt(i);
        if (QWidget* w = it->widget()) {
            w->show();
            want += qMax(w->sizeHint().height(), w->minimumHeight());
        } else {
            want += it->sizeHint().height();
        }
        if (i) want += c->layout()->spacing();
    }
    c->layout()->activate();
    const int avail = parentWidget()
        ? qMax(240, int(parentWidget()->height() * 0.92)) : want;
    const int h = qMin(want, avail);
    scroll_->setMinimumHeight(h);
    card()->setMinimumHeight(h);   // sizeHint скролла мал — тянем карточку
}

void ProfilePanel::renderSkeleton() {
    stack_->setCurrentIndex(0);
    clearContent();
    col_->insertWidget(0, new ProfileSkeleton());
    fitHeight();
}

void ProfilePanel::renderError(const QString& message) {
    stack_->setCurrentIndex(0);
    clearContent();
    auto* box = new QWidget();
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(16, 40, 16, 40);
    v->setSpacing(12);
    auto* title = new QLabel(QStringLiteral("Не удалось открыть профиль"), box);
    title->setObjectName(QStringLiteral("profErrorTitle"));
    title->setAlignment(Qt::AlignHCenter);
    auto* text = new QLabel(message, box);
    text->setObjectName(QStringLiteral("profErrorText"));
    text->setAlignment(Qt::AlignHCenter);
    text->setWordWrap(true);
    auto* retry = new QPushButton(QStringLiteral("Попробовать снова"), box);
    retry->setObjectName(QStringLiteral("primaryBtn"));
    retry->setCursor(Qt::PointingHandCursor);
    connect(retry, &QPushButton::clicked, this, [this]() {
        profileCache().remove(QStringLiteral("id:") + userId_);
        hasPreview_ = false;
        openFor(userId_);
    });
    v->addWidget(title);
    v->addWidget(text);
    v->addWidget(retry, 0, Qt::AlignHCenter);
    col_->insertWidget(0, box);
    fitHeight();
}

void ProfilePanel::renderProfile(const QJsonObject& data) {
    lastData_ = data;
    mediaRow_ = nullptr;
    const QJsonObject p = data.value(QStringLiteral("profile")).toObject();
    const QJsonObject rel = data.value(QStringLiteral("relation")).toObject();
    const bool isPreview = data.value(QStringLiteral("is_preview")).toBool(false);
    isSelf_ = rel.value(QStringLiteral("is_self")).toBool(false);

    stack_->setCurrentIndex(0);
    clearContent();
    int at = 0;
    auto add = [&](QWidget* w) { col_->insertWidget(at++, w); };

    add(makeHead(p));

    // Предупреждение о системном аккаунте — сразу под шапкой.
    const QJsonObject notice = p.value(QStringLiteral("notice")).toObject();
    if (notice.contains(QStringLiteral("text"))) {
        auto* n = new QLabel(notice.value(QStringLiteral("text")).toString());
        n->setWordWrap(true);
        n->setStyleSheet(QStringLiteral(
            "color:#F3F1F8;font-size:13px;background:rgba(217,160,91,0.14);"
            "border:1px solid #D9A05B;border-radius:14px;padding:12px 16px;"));
        auto* wrap = new QWidget();
        auto* wl = new QHBoxLayout(wrap);
        wl->setContentsMargins(16, 12, 16, 0);
        wl->addWidget(n);
        add(wrap);
    }

    // Действия не рисуем на превью: их состав знает только сервер.
    if (!isPreview) add(makeActions(p, rel));

    if (QWidget* info = makeInfo(p)) add(info);
    const QJsonArray marks = data.value(QStringLiteral("marks")).toArray();
    if (!marks.isEmpty()) add(makeMarks(marks));
    const QJsonObject gifts = data.value(QStringLiteral("gifts")).toObject();
    if (gifts.value(QStringLiteral("total")).toInt(0) > 0) add(makeGifts(gifts));
    if (!isPreview && !isSelf_ && !p.value(QStringLiteral("id")).toString().isEmpty())
        add(makeMediaEntry(p));
    if (!isPreview)
        if (QWidget* bottom = makeBottom(p, rel)) add(bottom);
    fitHeight();
}

QWidget* ProfilePanel::makeHead(const QJsonObject& p) {
    return new HeadWidget(p, knownAvatar_);
}

QPushButton* ProfilePanel::makeAction(Icons::Kind icon, const QString& label, bool enabled) {
    auto* w = new QPushButton();
    w->setObjectName(QStringLiteral("profAct"));
    w->setCursor(Qt::PointingHandCursor);
    w->setFlat(true);
    w->setEnabled(enabled);
    w->setMinimumHeight(64);
    w->setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(4, 8, 4, 8);
    v->setSpacing(6);
    // Недоступное действие остаётся видимым, но явно погашено (.is-disabled).
    const QColor iconCol = enabled ? kText1 : QColor(0x4A, 0x46, 0x56);
    auto* ic = new QLabel(w);
    ic->setPixmap(Icons::pixmap(icon, 22, iconCol));
    ic->setAlignment(Qt::AlignCenter);
    ic->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto* lb = new QLabel(label, w);
    lb->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
        .arg(enabled ? QStringLiteral("#ACA6BD") : QStringLiteral("#4A4656")));
    lb->setAlignment(Qt::AlignHCenter);
    lb->setAttribute(Qt::WA_TransparentForMouseEvents);
    v->addWidget(ic, 0, Qt::AlignHCenter);
    v->addWidget(lb);
    w->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return w;
}

QWidget* ProfilePanel::makeActions(const QJsonObject& p, const QJsonObject& rel) {
    auto* row = new QWidget();
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(16, 0, 16, 16);
    lay->setSpacing(8);

    if (rel.value(QStringLiteral("is_self")).toBool(false)) {
        auto* saved = makeAction(Icons::Message, QStringLiteral("Избранное"), true);
        connect(saved, &QPushButton::clicked, this, [this]() {
            emit savedMessagesRequested();
            closeAnimated();
        });
        lay->addWidget(saved, 1);
        auto* edit = makeAction(Icons::More, QStringLiteral("Изменить"), true);
        connect(edit, &QPushButton::clicked, this, [this]() {
            emit settingsRequested();
            closeAnimated();
        });
        lay->addWidget(edit, 1);
        return row;
    }

    const bool canMsg = rel.value(QStringLiteral("can_message")).toBool(false);
    const bool canCall = rel.value(QStringLiteral("can_call")).toBool(false);

    auto* msg = makeAction(Icons::Message, QStringLiteral("Сообщение"), canMsg);
    connect(msg, &QPushButton::clicked, this, [this]() {
        emit messageRequested(userId_);
        closeAnimated();
    });
    lay->addWidget(msg, 1);

    auto* bell = makeAction(Icons::Bell, QStringLiteral("Звук"), canMsg);
    connect(bell, &QPushButton::clicked, this, [this, bell]() {
        setChatMuted(userId_, !isChatMuted(userId_));
        emit muteToggled(userId_);
        // is-off в вебе гасит иконку, но кнопка остаётся рабочей.
        if (auto* bl = bell->layout()) {
            if (auto* ic = qobject_cast<QLabel*>(bl->itemAt(0)->widget()))
                ic->setPixmap(Icons::pixmap(Icons::Bell, 22,
                    isChatMuted(userId_) ? kText2 : kText1));
        }
    });
    lay->addWidget(bell, 1);

    auto* call = makeAction(Icons::Phone, QStringLiteral("Позвонить"), canCall);
    connect(call, &QPushButton::clicked, this, [this, p]() {
        emit callRequested(userId_,
                           p.value(QStringLiteral("display_name")).toString(),
                           p.value(QStringLiteral("avatar_url")).toString());
        closeAnimated();
    });
    lay->addWidget(call, 1);

    auto* more = makeAction(Icons::More, QStringLiteral("Ещё"), true);
    connect(more, &QPushButton::clicked, this, [this, more]() {
        moreMenu(more->mapToGlobal(QPoint(0, more->height())));
    });
    lay->addWidget(more, 1);
    return row;
}

QWidget* ProfilePanel::makeInfoRow(Icons::Kind icon, const QString& value,
                                   const QString& label, bool copyable) {
    auto* row = new QFrame();
    row->setObjectName(QStringLiteral("profRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    if (copyable) {
        // is-copyable: курсор-рука и подсветка при наведении, клик копирует.
        row->setStyleSheet(QStringLiteral(
            "QFrame#profRow{border-radius:12px;}"
            "QFrame#profRow:hover{background:#221F2C;}"));
        row->setCursor(Qt::PointingHandCursor);
        row->setToolTip(QStringLiteral("Нажмите, чтобы скопировать"));
        row->installEventFilter(new SuperSearchClickFilter([value]() {
            QApplication::clipboard()->setText(value);
        }, row));
    }
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(10);
    auto* chip = new QLabel(row);
    chip->setFixedSize(34, 34);
    chip->setAlignment(Qt::AlignCenter);
    chip->setStyleSheet(QStringLiteral("background:#221F2C;border-radius:10px;"));
    chip->setPixmap(Icons::pixmap(icon, 20, kRowIcon));
    lay->addWidget(chip, 0, Qt::AlignTop);
    auto* col = new QVBoxLayout();
    col->setSpacing(2);
    auto* v = new QLabel(value, row);
    v->setObjectName(QStringLiteral("profRowValue"));
    v->setWordWrap(true);
    v->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* c = new QLabel(label, row);
    c->setObjectName(QStringLiteral("profRowLabel"));
    col->addWidget(v);
    col->addWidget(c);
    lay->addLayout(col, 1);
    return row;
}

QWidget* ProfilePanel::makeChannelRow(const QJsonObject& ch) {
    auto* row = new QFrame();
    row->setObjectName(QStringLiteral("profChanRow"));
    row->setAttribute(Qt::WA_StyledBackground, true);
    row->setCursor(Qt::PointingHandCursor);
    row->setStyleSheet(QStringLiteral(
        "QFrame#profChanRow{background:#221F2C;border-radius:12px;}"
        "QFrame#profChanRow:hover{background:#2A2637;}"));
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(10);
    auto* chip = new QLabel(row);
    chip->setFixedSize(34, 34);
    chip->setAlignment(Qt::AlignCenter);
    chip->setStyleSheet(QStringLiteral(
        "background:rgba(124,108,255,0.16);border-radius:10px;"));
    chip->setPixmap(Icons::pixmap(Icons::Megaphone, 20, kLinkAccent));
    lay->addWidget(chip);
    auto* col = new QVBoxLayout();
    col->setSpacing(2);
    auto* name = new QLabel(ch.value(QStringLiteral("name")).toString(
        QStringLiteral("Канал")), row);
    name->setObjectName(QStringLiteral("profChanValue"));
    auto* sub = new QLabel(ch.value(QStringLiteral("is_private")).toBool()
        ? QStringLiteral("Личный канал") : QStringLiteral("Канал"), row);
    sub->setObjectName(QStringLiteral("profRowLabel"));
    col->addWidget(name);
    col->addWidget(sub);
    lay->addLayout(col, 1);
    auto* chev = new QLabel(row);
    chev->setPixmap(Icons::pixmap(Icons::ChevronRight, 16, kText3));
    lay->addWidget(chev, 0, Qt::AlignVCenter);

    const QString id = ch.value(QStringLiteral("id")).toString();
    const QString nm = ch.value(QStringLiteral("name")).toString();
    const QString link = ch.value(QStringLiteral("custom_link")).toString();
    // Канал — переход: профиль закрывается, открывается канал (как в вебе).
    row->installEventFilter(new SuperSearchClickFilter([this, id, nm, link]() {
        emit channelOpenRequested(id, nm, link);
        closeAnimated();
    }, row));
    return row;
}

QWidget* ProfilePanel::makeInfo(const QJsonObject& p) {
    QList<QWidget*> rows;
    const QJsonObject ch = p.value(QStringLiteral("personal_channel")).toObject();
    if (!ch.value(QStringLiteral("id")).toString().isEmpty()) rows << makeChannelRow(ch);

    const QString bio = p.value(QStringLiteral("bio")).toString();
    if (!bio.isEmpty()) rows << makeInfoRow(Icons::About, bio, QStringLiteral("О себе"));

    const QString bd = birthdayText(p);
    if (!bd.isEmpty()) rows << makeInfoRow(Icons::Cake, bd, QStringLiteral("День рождения"));

    const QString uname = p.value(QStringLiteral("username")).toString();
    if (!uname.isEmpty())
        rows << makeInfoRow(Icons::At, QStringLiteral("@") + uname,
                            QStringLiteral("Имя пользователя"), /*copyable=*/true);

    const QString joined = joinedText(p);
    if (!joined.isEmpty())
        rows << makeInfoRow(Icons::Calendar, joined, QStringLiteral("В Xipher с"));

    // Часы работы (business_hours): строкой «Пн–Пт 09:00–18:00».
    const QJsonObject bh = p.value(QStringLiteral("business_hours")).toObject();
    if (!bh.isEmpty()) {
        static const char* days[] = {"mon","tue","wed","thu","fri","sat","sun"};
        QStringList parts;
        for (const char* d : days) {
            const QJsonObject o = bh.value(QLatin1String(d)).toObject();
            if (!o.value(QStringLiteral("enabled")).toBool(false)) continue;
            const QString t = QStringLiteral("%1–%2")
                .arg(o.value(QStringLiteral("start")).toString(),
                     o.value(QStringLiteral("end")).toString());
            if (!parts.contains(t)) parts << t;
        }
        if (!parts.isEmpty())
            rows << makeInfoRow(Icons::Clock, parts.join(QStringLiteral(", ")),
                                QStringLiteral("Часы работы"));
    }

    if (rows.isEmpty()) return nullptr;

    auto* sec = new QFrame();
    sec->setObjectName(QStringLiteral("profSec"));
    sec->setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(sec);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(0);
    for (QWidget* r : rows) v->addWidget(r);

    if (!uname.isEmpty()) {
        auto* qr = new QPushButton(QStringLiteral("Показать QR-код профиля"), sec);
        qr->setObjectName(QStringLiteral("profLinkBtn"));
        qr->setCursor(Qt::PointingHandCursor);
        connect(qr, &QPushButton::clicked, this, &ProfilePanel::openQr);
        v->addWidget(qr);
    }

    auto* wrap = new QWidget();
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(16, 0, 16, 12);
    wl->addWidget(sec);
    return wrap;
}

QWidget* ProfilePanel::makeMarks(const QJsonArray& marks) {
    auto* sec = new QFrame();
    sec->setObjectName(QStringLiteral("profSec"));
    sec->setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(sec);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(0);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(8, 8, 8, 4);
    auto* title = new QLabel(QStringLiteral("Знаки"), sec);
    title->setObjectName(QStringLiteral("profSecTitle"));
    head->addWidget(title, 1);
    v->addLayout(head);

    auto markRow = [&](const QJsonObject& m, QWidget* host) {
        auto* row = new QWidget(host);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(8, 8, 8, 8);
        rl->setSpacing(12);
        auto* icon = new QLabel(row);
        icon->setFixedSize(38, 38);
        icon->setAlignment(Qt::AlignCenter);
        icon->setStyleSheet(QStringLiteral("background:#221F2C;border-radius:19px;"));
        icon->setPixmap(Icons::pixmap(Icons::Star, 22, kAccentText));
        rl->addWidget(icon);
        auto* body = new QVBoxLayout();
        body->setSpacing(1);
        auto* t = new QLabel(m.value(QStringLiteral("title")).toString(), row);
        t->setObjectName(QStringLiteral("profMarkTitle"));
        auto* meta = new QLabel(m.value(QStringLiteral("description")).toString(), row);
        meta->setObjectName(QStringLiteral("profMarkMeta"));
        meta->setWordWrap(true);
        body->addWidget(t);
        body->addWidget(meta);
        rl->addLayout(body, 1);
        return row;
    };
    // Свёрнуто до трёх: коллекция не оттесняет действия и сведения.
    for (int i = 0; i < qMin(marks.size(), 3); ++i)
        v->addWidget(markRow(marks[i].toObject(), sec));

    if (marks.size() > 3) {
        auto* more = new QPushButton(
            QStringLiteral("Показать все (%1)").arg(marks.size()), sec);
        more->setObjectName(QStringLiteral("profMoreBtn"));
        more->setCursor(Qt::PointingHandCursor);
        v->addWidget(more);
        connect(more, &QPushButton::clicked, this, [more, v, marks, markRow]() {
            for (int i = 3; i < marks.size(); ++i)
                v->insertWidget(v->indexOf(more), markRow(marks[i].toObject(), more));
            more->hide();
        });
    }

    auto* wrap = new QWidget();
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(16, 0, 16, 12);
    wl->addWidget(sec);
    return wrap;
}


// Ряд карточек подарков (закреплённые, а если их нет — недавние): до трёх,
// как LIMIT 3 в /api/profile/view.
void ProfilePanel::fillGiftsRow(const QJsonArray& list) {
    if (!giftsRow_) return;
    if (giftsRow_->layout()) {
        QLayoutItem* it;
        while ((it = giftsRow_->layout()->takeAt(0)) != nullptr) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        delete giftsRow_->layout();
    }
    auto* grid = new QGridLayout(giftsRow_);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setSpacing(8);
    int i = 0;
    for (const QJsonValue& gv : list) {
        if (i >= 3) break;
        const QJsonObject g = gv.toObject();
        auto* card = new QWidget(giftsRow_);
        card->setObjectName(QStringLiteral("profGift"));
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setStyleSheet(QStringLiteral(
            "QWidget#profGift{background:#221F2C;border-radius:14px;}"));
        card->setMinimumHeight(120);
        auto* cl = new QVBoxLayout(card);
        cl->setContentsMargins(8, 12, 8, 12);
        cl->setSpacing(6);
        // Векторный арт каталога (GiftArt, 1:1 с gift-art.js веба): слаг из
        // visual_key/gift_id/icon — сырой текст «crown» здесь был багом.
        const QString slug = g.value(QStringLiteral("visual_key")).toString(
            g.value(QStringLiteral("gift_id")).toString(
                g.value(QStringLiteral("icon")).toString(
                    g.value(QStringLiteral("art")).toString())));
        auto* art = new QLabel(card);
        art->setAlignment(Qt::AlignCenter);
        art->setPixmap(GiftArt::pixmap(slug, 52));
        auto* nm = new QLabel(g.value(QStringLiteral("name")).toString(
            GiftArt::name(slug).isEmpty() ? QStringLiteral("Подарок")
                                          : GiftArt::name(slug)), card);
        nm->setObjectName(QStringLiteral("profGiftName"));
        nm->setAlignment(Qt::AlignHCenter);
        nm->setWordWrap(true);
        cl->addWidget(art, 1);
        cl->addWidget(nm);
        const QString msg = g.value(QStringLiteral("message")).toString();
        if (!msg.isEmpty()) card->setToolTip(msg);
        grid->addWidget(card, 0, i);
        ++i;
    }
    fitHeight();
}

// Экран всей коллекции — как openCollection веба: карточки с «от кого ·
// когда», подписью и отметкой закрепления.
void ProfilePanel::buildCollectionScreen(QWidget* host) {
    if (!host) return;
    if (host->layout()) {
        QLayoutItem* it;
        while ((it = host->layout()->takeAt(0)) != nullptr) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        delete host->layout();
    }
    auto* hv = new QVBoxLayout(host);
    hv->setContentsMargins(16, 12, 16, 16);
    hv->setSpacing(8);
    if (allGiftsHidden_) {
        auto* note = new QLabel(QStringLiteral("Этот человек скрыл свои подарки"), host);
        note->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;padding:16px;"));
        hv->addWidget(note);
        return;
    }
    if (allGifts_.isEmpty()) {
        auto* note = new QLabel(isSelf_ ? QStringLiteral("Вам пока не дарили подарков")
                                        : QStringLiteral("Подарков пока нет"), host);
        note->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;padding:16px;"));
        hv->addWidget(note);
        return;
    }
    for (const QJsonValue& gv : allGifts_) {
        const QJsonObject g = gv.toObject();
        const bool pinned = g.value(QStringLiteral("pinned")).toBool(false);
        auto* card = new QWidget(host);
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setStyleSheet(QStringLiteral(
            "QWidget{background:#1A1822;border-radius:20px;%1}")
            .arg(pinned ? QStringLiteral("border:1px solid rgba(139,92,246,0.34);")
                        : QString()));
        auto* hl = new QHBoxLayout(card);
        hl->setContentsMargins(12, 12, 12, 12);
        hl->setSpacing(12);
        // Векторный арт + локальное имя/редкость/номер из каталога (GiftArt).
        const QString slug = g.value(QStringLiteral("art")).toString(
            g.value(QStringLiteral("visual_key")).toString(
                g.value(QStringLiteral("gift_id")).toString()));
        auto* art = new QLabel(card);
        art->setPixmap(GiftArt::pixmap(slug, 44));
        auto* col2 = new QVBoxLayout();
        col2->setSpacing(2);
        const QString localName = GiftArt::name(slug);
        auto* nm = new QLabel(g.value(QStringLiteral("name")).toString(
            localName.isEmpty() ? QStringLiteral("Подарок") : localName), card);
        nm->setStyleSheet(QStringLiteral(
            "color:#F3F1F8;font-size:14px;font-weight:600;background:transparent;"));
        col2->addWidget(nm);
        // Чип редкости (xp-gift-rar веба): цвет по разряду каталога,
        // серверный признак тиража редкость только поднимает.
        {
            static const auto tierColor = [](const QString& tier) {
                if (tier == QLatin1String("legendary")) return QStringLiteral("#F5C518");
                if (tier == QLatin1String("epic"))      return QStringLiteral("#B78BFA");
                if (tier == QLatin1String("rare"))      return QStringLiteral("#6FB1FC");
                return QStringLiteral("#726C82");
            };
            const QString tier = GiftArt::rarityFor(
                slug, g.value(QStringLiteral("rarity")).toString());
            auto* rar = new QLabel(GiftArt::rarityLabel(tier), card);
            rar->setStyleSheet(QStringLiteral(
                "color:%1;font-size:11px;font-weight:600;background:transparent;")
                .arg(tierColor(tier)));
            col2->addWidget(rar);
        }
        if (pinned) {
            auto* pin = new QLabel(QStringLiteral("Закреплён"), card);
            pin->setStyleSheet(QStringLiteral(
                "color:#BBA4FF;font-size:11px;background:transparent;"));
            col2->addWidget(pin);
        }
        // «№ 12345 · от @user · 22 сентября» — как xp-gc__meta; номер
        // экземпляра стабильный (FNV-1a от UUID копии, как в вебе).
        QStringList meta;
        const int instanceNo = GiftArt::number(g.value(QStringLiteral("id")).toString());
        if (instanceNo > 0) meta << QStringLiteral("№ %1").arg(instanceNo);
        const QString from = g.value(QStringLiteral("from")).toString();
        if (!from.isEmpty()) meta << QStringLiteral("от @") + from;
        const QDateTime dt = parseServerTime(g.value(QStringLiteral("created_at")).toString());
        if (dt.isValid())
            meta << QStringLiteral("%1 %2").arg(dt.date().day())
                    .arg(QString::fromUtf8(kMonths[dt.date().month()]));
        if (!meta.isEmpty()) {
            auto* m = new QLabel(meta.join(QStringLiteral(" · ")), card);
            m->setStyleSheet(QStringLiteral(
                "color:#726C82;font-size:12px;background:transparent;"));
            col2->addWidget(m);
        }
        const QString msg = g.value(QStringLiteral("message")).toString();
        if (!msg.isEmpty()) {
            auto* m2 = new QLabel(msg, card);
            m2->setWordWrap(true);
            m2->setStyleSheet(QStringLiteral(
                "color:#ACA6BD;font-size:12px;font-style:italic;background:transparent;"));
            col2->addWidget(m2);
        }
        hl->addWidget(art);
        hl->addLayout(col2, 1);
        hv->addWidget(card);
    }
    hv->addStretch(1);
}

QWidget* ProfilePanel::makeGifts(const QJsonObject& gifts) {
    auto* sec = new QFrame();
    sec->setObjectName(QStringLiteral("profSec"));
    sec->setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(sec);
    v->setContentsMargins(8, 8, 8, 8);
    v->setSpacing(0);

    auto* head = new QHBoxLayout();
    head->setContentsMargins(8, 8, 8, 4);
    auto* title = new QLabel(QStringLiteral("Подарки"), sec);
    title->setObjectName(QStringLiteral("profSecTitle"));
    auto* count = new QLabel(QString::number(
        gifts.value(QStringLiteral("total")).toInt(0)), sec);
    count->setObjectName(QStringLiteral("profSecCount"));
    head->addWidget(title, 1);
    head->addWidget(count);
    v->addLayout(head);

    const QJsonArray pinned = gifts.value(QStringLiteral("pinned")).toArray();
    giftsRow_ = new QWidget(sec);
    giftsRow_->setStyleSheet(QStringLiteral("background:transparent;"));
    v->addWidget(giftsRow_);
    if (!pinned.isEmpty()) {
        fillGiftsRow(pinned);
    } else if (!allGifts_.isEmpty()) {
        fillGiftsRow(allGifts_);   // уже загружены (повторное открытие)
    } else {
        // Ничего не закреплено, но подарки есть: показываем последние,
        // пока едет /api/gifts/of-user (в вебе закреплённые — выбор владельца;
        // пустой ряд читался бы как «подарков нет»).
        giftsInlinePending_ = true;
        api_->giftsOfUser(userId_);
    }

    auto* all = new QPushButton(QStringLiteral("Показать все подарки"), sec);
    all->setObjectName(QStringLiteral("profMoreBtn"));
    all->setCursor(Qt::PointingHandCursor);
    connect(all, &QPushButton::clicked, this, [this]() {
        // Вся коллекция — вложенным экраном с шапкой «назад» (openCollection).
        auto* host = new QWidget();
        host->setStyleSheet(QStringLiteral("background:transparent;"));
        auto* hv = new QVBoxLayout(host);
        hv->setContentsMargins(16, 12, 16, 16);
        hv->setSpacing(8);
        if (allGifts_.isEmpty() && !allGiftsHidden_) {
            auto* loading = new QLabel(QStringLiteral("Загрузка…"), host);
            loading->setStyleSheet(QStringLiteral(
                "color:#726C82;font-size:13px;padding:16px;background:transparent;"));
            hv->addWidget(loading);
            collectionScreen_ = host;
            if (!allGiftsRequested_) { allGiftsRequested_ = true; api_->giftsOfUser(userId_); }
        } else {
            buildCollectionScreen(host);
        }
        pushScreen(QStringLiteral("Подарки"), host);
    });
    v->addWidget(all);

    auto* wrap = new QWidget();
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(16, 0, 16, 12);
    wl->addWidget(sec);
    return wrap;
}


// Экран «Общие медиа» (xp-md веба): чипы категорий + сетка плиток.
void ProfilePanel::openMediaScreen() {
    auto* host = new QWidget();
    host->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    // Чипы категорий с счётчиками (без category сервер отдаёт counts/total).
    auto* chips = new QWidget(host);
    chips->setStyleSheet(QStringLiteral(
        "QWidget{background:#131218;border-bottom:1px solid #221F2C;}"));
    chips->setAttribute(Qt::WA_StyledBackground, true);
    auto* chl = new QHBoxLayout(chips);
    chl->setContentsMargins(12, 8, 12, 8);
    chl->setSpacing(8);

    auto* gridHost = new QWidget(host);
    auto* gl = new QVBoxLayout(gridHost);
    gl->setContentsMargins(12, 12, 12, 16);
    auto* loading = new QLabel(QStringLiteral("Загрузка…"), gridHost);
    loading->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;padding:16px;"));
    gl->addWidget(loading);
    auto* body = new QGridLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(4);
    gl->addLayout(body);

    const auto rebuild = [gridHost, body](const QJsonArray& items) {
        QLayoutItem* it;
        while ((it = body->takeAt(0)) != nullptr) {
            if (it->widget()) it->widget()->deleteLater();
            delete it;
        }
        if (items.isEmpty()) {
            auto* empty = new QLabel(QStringLiteral("Ничего нет"), gridHost);
            empty->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;padding:16px;"));
            body->addWidget(empty, 0, 0);
            return;
        }
        int row = 0, col = 0;
        for (const QJsonValue& iv : items) {
            const QJsonObject o = iv.toObject();
            const QString type = o.value(QStringLiteral("type")).toString();
            auto* tile = new QWidget(gridHost);
            tile->setObjectName(QStringLiteral("mediaTile"));
            tile->setStyleSheet(QStringLiteral(
                "QWidget#mediaTile{background:#221F2C;border-radius:12px;}"));
            tile->setAttribute(Qt::WA_StyledBackground, true);
            tile->setFixedSize(96, 96);
            auto* tl = new QVBoxLayout(tile);
            tl->setContentsMargins(6, 6, 6, 6);
            const QString glyph = type == QStringLiteral("video") ? QString::fromUtf8("\U0001F3AC")
                             : type == QStringLiteral("voice") ? QString::fromUtf8("\U0001F3A4")
                             : type == QStringLiteral("audio") ? QString::fromUtf8("\u266A")
                             : type == QStringLiteral("link")  ? QString::fromUtf8("\U0001F517")
                             : type == QStringLiteral("file")  ? QString::fromUtf8("\U0001F4C4")
                                                               : QString::fromUtf8("\U0001F5BC");
            auto* ic = new QLabel(glyph, tile);
            ic->setAlignment(Qt::AlignCenter);
            ic->setStyleSheet(QStringLiteral("font-size:26px;background:transparent;"));
            tl->addWidget(ic, 1);
            const QString nm = o.value(QStringLiteral("name")).toString(
                o.value(QStringLiteral("title")).toString());
            if (!nm.isEmpty()) {
                auto* nmL = new QLabel(nm, tile);
                nmL->setStyleSheet(QStringLiteral(
                    "color:#ACA6BD;font-size:10px;background:transparent;"));
                nmL->setWordWrap(true);
                tl->addWidget(nmL);
            }
            body->addWidget(tile, row, col);
            if (++col >= 6) { col = 0; ++row; }
        }
    };

    struct Cat { const char* key; const char* label; };
    static const Cat cats[] = {
        {"photo", "Фото"}, {"video", "Видео"}, {"voice", "Голосовые"},
        {"audio", "Аудио"}, {"file", "Файлы"}, {"link", "Ссылки"},
    };
    QList<QPair<QString, QPushButton*>> chipList;
    for (const Cat& c : cats) {
        auto* chip = new QPushButton(QString::fromUtf8(c.label), chips);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setCheckable(true);
        chip->setStyleSheet(QStringLiteral(
            "QPushButton{border:1px solid #2B2737;border-radius:999px;padding:6px 14px;"
            "background:transparent;color:#ACA6BD;font-size:12px;}"
            "QPushButton:checked{background:#8B5CF6;border-color:#8B5CF6;color:#fff;}"));
        chl->addWidget(chip);
        chipList.append({QString::fromLatin1(c.key), chip});
    }
    v->addWidget(chips);
    v->addWidget(gridHost, 1);

    const QString peer = userId_;
    // Счётчики на чипы.
    connect(api_, &ApiClient::mediaCountsLoaded, host,
            [chipList](const QString&, const QJsonObject& counts, int total) {
        for (auto& pair : chipList) {
            const int n = pair.first == QLatin1String("all")
                ? total : counts.value(pair.first).toInt(0);
            pair.second->setText(QStringLiteral("%1 · %2")
                                     .arg(pair.second->text().section(QStringLiteral(" ·"), 0, 0))
                                     .arg(n));
        }
    });
    // Предметы по клику на чип (первый — фото — грузим сразу).
    connect(api_, &ApiClient::mediaListLoaded, host,
            [rebuild, loading](const QString&, const QJsonArray& items) {
        if (loading) loading->hide();
        rebuild(items);
    });
    for (auto& pair : chipList) {
        const QString cat = pair.first;
        connect(pair.second, &QPushButton::clicked, host, [this, cat, peer]() {
            api_->requestMediaList(peer, cat);
        });
    }
    api_->requestMediaCounts(peer);
    chipList.first().second->setChecked(true);
    api_->requestMediaList(peer, QString::fromLatin1("photo"));
    pushScreen(QStringLiteral("Общие медиа"), host);
}

QWidget* ProfilePanel::makeMediaEntry(const QJsonObject& p) {
    const QString id = p.value(QStringLiteral("id")).toString();
    auto* sec = new QFrame();
    sec->setObjectName(QStringLiteral("profSec"));
    sec->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QHBoxLayout(sec);
    hl->setContentsMargins(12, 4, 12, 4);
    auto* btn = new QPushButton(QStringLiteral("Общие медиа"), sec);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;color:#BBA4FF;"
        "font-size:14px;text-align:left;padding:8px 0;}"));
    btn->setCursor(Qt::PointingHandCursor);
    auto* cnt = new QLabel(mediaTotal_ > 0 ? QString::number(mediaTotal_) : QString(), sec);
    cnt->setObjectName(QStringLiteral("profMediaCount"));
    cnt->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:13px;"));
    hl->addWidget(btn, 1);
    hl->addWidget(cnt, 0, Qt::AlignVCenter);
    connect(btn, &QPushButton::clicked, this, [this]() { openMediaScreen(); });
    mediaRow_ = sec;
    // Ноль кэшируется наравне с числом: без медиа строки нет (renderMediaEntry).
    sec->setVisible(mediaTotal_ != 0);

    auto* wrap = new QWidget();
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(16, 0, 16, 12);
    wl->addWidget(sec);
    return wrap;
}

QWidget* ProfilePanel::makeBottom(const QJsonObject& p, const QJsonObject& rel) {
    struct Item { QString label; bool danger; std::function<void()> run; };
    QList<Item> items;
    const bool isContact = rel.value(QStringLiteral("is_contact")).toBool(false);
    const bool blocked = rel.value(QStringLiteral("is_blocked_by_me")).toBool(false);
    const QString uname = p.value(QStringLiteral("username")).toString();
    const QString displayName = p.value(QStringLiteral("display_name")).toString();

    if (blocked) {
        items << Item{QStringLiteral("Разблокировать"), false, [this]() {
            api_->unblockUser(userId_);
            afterAction();
        }};
    } else {
        if (isContact) {
            items << Item{QStringLiteral("Изменить контакт"), false, [this, displayName]() {
                bool ok = false;
                const QString name = QInputDialog::getText(this,
                    QStringLiteral("Контакт"), QStringLiteral("Имя контакта"),
                    QLineEdit::Normal, displayName, &ok);
                if (ok && !name.trimmed().isEmpty())
                    api_->setContactName(userId_, name.trimmed());
            }};
        }
        items << Item{QStringLiteral("Поделиться контактом"), false, [uname]() {
            QApplication::clipboard()->setText(
                QStringLiteral("https://messenger.xipher.pro/@") + uname);
        }};
        if (isContact) {
            items << Item{QStringLiteral("Удалить контакт"), true, [this]() {
                api_->removeFriend(userId_);
                afterAction();
            }};
        }
        items << Item{QStringLiteral("Заблокировать"), true, [this]() {
            if (QMessageBox::question(this, QStringLiteral("Блокировка"),
                    QStringLiteral("Заблокировать пользователя?")) == QMessageBox::Yes) {
                api_->blockUser(userId_);
                afterAction();
            }
        }};
    }
    if (items.isEmpty()) return nullptr;

    auto* box = new QFrame();
    box->setObjectName(QStringLiteral("profBottom"));
    box->setStyleSheet(QStringLiteral("QFrame#profBottom{background:#1A1822;border-radius:20px;}"));
    box->setAttribute(Qt::WA_StyledBackground, true);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(0);
    for (const Item& it : items) {
        auto* b = new QPushButton(it.label, box);
        b->setObjectName(it.danger ? QStringLiteral("profBottomItemDanger")
                                   : QStringLiteral("profBottomItem"));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, it.run);
        v->addWidget(b);
    }
    auto* wrap = new QWidget();
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(16, 16, 16, 32);
    wl->addWidget(box);
    return wrap;
}

void ProfilePanel::moreMenu(const QPoint& globalPos) {
    const QJsonObject p = lastData_.value(QStringLiteral("profile")).toObject();
    const QJsonObject rel = lastData_.value(QStringLiteral("relation")).toObject();
    const QString type = p.value(QStringLiteral("account_type")).toString();
    const bool isBot = type == QLatin1String("bot") || type == QLatin1String("official_bot");
    const QString uname = p.value(QStringLiteral("username")).toString();
    const QString displayName = p.value(QStringLiteral("display_name")).toString();

    QMenu m(this);
    m.setStyleSheet(QLatin1String(kPanelQss));
    if (rel.value(QStringLiteral("can_gift")).toBool(false) && !isBot) {
        connect(m.addAction(QStringLiteral("Отправить подарок")), &QAction::triggered,
                this, [this]() { openGiftDialog(); });
    }
    if (rel.value(QStringLiteral("is_contact")).toBool(false)) {
        connect(m.addAction(QStringLiteral("Изменить контакт")), &QAction::triggered,
                this, [this, displayName]() {
            bool ok = false;
            const QString name = QInputDialog::getText(this,
                QStringLiteral("Контакт"), QStringLiteral("Имя контакта"),
                QLineEdit::Normal, displayName, &ok);
            if (ok && !name.trimmed().isEmpty())
                api_->setContactName(userId_, name.trimmed());
        });
    } else if (!isBot) {
        connect(m.addAction(QStringLiteral("Добавить в контакты")), &QAction::triggered,
                this, [this, uname]() {
            api_->sendFriendRequest(uname);
            afterAction();
        });
    }
    connect(m.addAction(QStringLiteral("Поделиться контактом")), &QAction::triggered,
            this, [uname]() {
        QApplication::clipboard()->setText(
            QStringLiteral("https://messenger.xipher.pro/@") + uname);
    });
    m.addSeparator();
    if (rel.value(QStringLiteral("is_blocked_by_me")).toBool(false)) {
        connect(m.addAction(QStringLiteral("Разблокировать")), &QAction::triggered,
                this, [this]() { api_->unblockUser(userId_); afterAction(); });
    } else {
        connect(m.addAction(QStringLiteral("Заблокировать")), &QAction::triggered,
                this, [this]() {
            if (QMessageBox::question(this, QStringLiteral("Блокировка"),
                    QStringLiteral("Заблокировать пользователя?")) == QMessageBox::Yes) {
                api_->blockUser(userId_);
                afterAction();
            }
        });
    }
    m.exec(globalPos);
}

// Действие изменило отношения — профиль перечитывается с сервера целиком
// (reload в view.js): клиент не угадывает новое состояние.
void ProfilePanel::afterAction() {
    profileCache().remove(QStringLiteral("id:") + userId_);
    api_->profileView(userId_);
    reqId_ = api_->profileViewReply();
}

void ProfilePanel::openQr() {
    const QString uname = lastData_.value(QStringLiteral("profile"))
                              .toObject().value(QStringLiteral("username")).toString();
    const QString link = QStringLiteral("https://messenger.xipher.pro/@") + uname;

    auto* host = new QWidget();
    host->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(host);
    v->setContentsMargins(16, 24, 16, 16);
    v->setSpacing(16);

    auto* qrBox = new QWidget(host);
    qrBox->setStyleSheet(QStringLiteral(
        "QWidget{background:#FFFFFF;border-radius:14px;}"));
    qrBox->setAttribute(Qt::WA_StyledBackground, true);
    auto* qrLay = new QVBoxLayout(qrBox);
    qrLay->setContentsMargins(14, 14, 14, 14);
    try {
        const qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
            link.toUtf8().constData(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int scale = 8, border = 2;
        const int dim = qr.getSize() + border * 2;
        const qreal dpr = 2.0;
        QPixmap pm(int(dim * scale * dpr), int(dim * scale * dpr));
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::white);
        QPainter pt(&pm);
        pt.setPen(Qt::NoPen);
        pt.setBrush(QColor(0x0B, 0x0A, 0x0E));
        for (int y = 0; y < qr.getSize(); ++y)
            for (int x = 0; x < qr.getSize(); ++x)
                if (qr.getModule(x, y))
                    pt.drawRect((border + x) * scale, (border + y) * scale, scale, scale);
        pt.end();
        auto* img = new QLabel(qrBox);
        img->setPixmap(pm);
        img->setStyleSheet(QStringLiteral("background:transparent;"));
        qrLay->addWidget(img, 0, Qt::AlignHCenter);
    } catch (...) {
        auto* fail = new QLabel(QStringLiteral("Не удалось построить QR-код"), qrBox);
        fail->setStyleSheet(QStringLiteral("color:#0B0A0E;font-size:13px;"));
        qrLay->addWidget(fail, 0, Qt::AlignHCenter);
    }

    auto* linkLbl = new QLabel(link, host);
    linkLbl->setStyleSheet(QStringLiteral(
        "color:#ACA6BD;font-size:13px;background:transparent;"));
    linkLbl->setAlignment(Qt::AlignHCenter);
    linkLbl->setWordWrap(true);

    auto* copy = new QPushButton(QStringLiteral("Скопировать ссылку"), host);
    copy->setObjectName(QStringLiteral("primaryBtn"));
    copy->setCursor(Qt::PointingHandCursor);
    connect(copy, &QPushButton::clicked, this, [copy, link]() {
        QApplication::clipboard()->setText(link);
        copy->setText(QStringLiteral("Скопировано"));
        QTimer::singleShot(1400, copy, [copy]() {
            copy->setText(QStringLiteral("Скопировать ссылку"));
        });
    });

    v->addWidget(qrBox, 0, Qt::AlignHCenter);
    v->addWidget(linkLbl);
    v->addWidget(copy, 0, Qt::AlignHCenter);
    v->addStretch(1);
    pushScreen(QStringLiteral("QR-код"), host);
}

// Вложенный экран с шапкой «назад» (embedPush веба).
void ProfilePanel::pushScreen(const QString& title, QWidget* screen) {
    auto* page = new QWidget();
    page->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto* bar = new QWidget();
    bar->setStyleSheet(QStringLiteral(
        "QWidget{background:#131218;border-bottom:1px solid #221F2C;}"));
    bar->setAttribute(Qt::WA_StyledBackground, true);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 8, 12, 8);
    bl->setSpacing(8);
    auto* back = new QPushButton(bar);
    back->setIcon(Icons::icon(Icons::ArrowLeft, 18, kText2));
    back->setFixedSize(32, 32);
    back->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;border-radius:16px;}"
        "QPushButton:hover{background:#221F2C;}"));
    connect(back, &QPushButton::clicked, this, &ProfilePanel::popScreen);
    auto* t = new QLabel(title, bar);
    t->setStyleSheet(QStringLiteral(
        "color:#F3F1F8;font-size:15px;font-weight:600;background:transparent;"));
    bl->addWidget(back);
    bl->addWidget(t, 1);
    v->addWidget(bar);
    v->addWidget(screen, 1);
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
    closeBtn_->raise();
}

void ProfilePanel::popScreen() {
    if (stack_->count() < 2) return;
    QWidget* top = stack_->widget(stack_->count() - 1);
    stack_->setCurrentIndex(0);
    top->deleteLater();
}

void ProfilePanel::openGiftDialog() {
    const QString toId = userId_;
    auto* ov = new ModalOverlay(window(), 460);
    ov->card()->setStyleSheet(QLatin1String(kPanelQss));
    ov->card()->setMinimumHeight(0);
    auto* cl = ov->cardLayout();
    cl->setContentsMargins(16, 16, 16, 16);
    auto* title = new QLabel(QStringLiteral("Отправить подарок"));
    title->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:17px;font-weight:700;"));
    cl->addWidget(title);

    auto* sa = new QScrollArea(ov->card());
    sa->setWidgetResizable(true);
    sa->setFrameShape(QFrame::NoFrame);
    sa->setStyleSheet(QLatin1String(kPanelQss));
    auto* gridWrap = new QWidget();
    gridWrap->setStyleSheet(QStringLiteral("background:transparent;"));
    sa->setWidget(gridWrap);
    cl->addWidget(sa, 1);

    auto* note = new QLineEdit(ov->card());
    note->setPlaceholderText(QStringLiteral("Записка к подарку (необязательно)"));
    cl->addWidget(note);

    auto* status = new QLabel(ov->card());
    status->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
    cl->addWidget(status);

    connect(api_, &ApiClient::giftsCatalogLoaded, ov,
            [this, ov, gridWrap, note, toId, status](const QJsonArray& gifts) {
        if (gifts.isEmpty()) {
            status->setText(QStringLiteral("Каталог недоступен"));
            return;
        }
        if (gridWrap->layout()) {
            QLayoutItem* it;
            while ((it = gridWrap->layout()->takeAt(0)) != nullptr) {
                if (it->widget()) it->widget()->deleteLater();
                delete it;
            }
            delete gridWrap->layout();
        }
        auto* g = new QGridLayout(gridWrap);
        g->setContentsMargins(0, 8, 0, 8);
        g->setSpacing(8);
        for (int i = 0; i < gifts.size(); ++i) {
            const QJsonObject gif = gifts[i].toObject();
            auto* cardBtn = new QPushButton(gridWrap);
            cardBtn->setObjectName(QStringLiteral("giftCard"));
            cardBtn->setStyleSheet(QStringLiteral(
                "QPushButton#giftCard{background:#1A1822;border:1px solid #2B2737;"
                "border-radius:20px;}"
                "QPushButton#giftCard:hover{background:#221F2C;border-color:#8B5CF6;}"));
            cardBtn->setCursor(Qt::PointingHandCursor);
            cardBtn->setFixedSize(100, 116);
            auto* vl = new QVBoxLayout(cardBtn);
            vl->setContentsMargins(6, 10, 6, 8);
            vl->setSpacing(4);
            auto* art = new QLabel(gif.value(QStringLiteral("icon")).toString(
                QStringLiteral("🎁")), cardBtn);
            art->setAlignment(Qt::AlignCenter);
            art->setStyleSheet(QStringLiteral("font-size:30px;"));
            auto* nm = new QLabel(gif.value(QStringLiteral("name")).toString(
                gif.value(QStringLiteral("title")).toString(
                    gif.value(QStringLiteral("id")).toString())), cardBtn);
            nm->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:11px;"));
            nm->setAlignment(Qt::AlignHCenter);
            nm->setWordWrap(true);
            auto* price = new QLabel(QStringLiteral("⭐ %1").arg(
                gif.value(QStringLiteral("price")).toInteger(0)), cardBtn);
            price->setStyleSheet(QStringLiteral("color:#F5C451;font-size:11px;"));
            price->setAlignment(Qt::AlignHCenter);
            vl->addWidget(art);
            vl->addWidget(nm, 1);
            vl->addWidget(price);
            const QString gid = gif.value(QStringLiteral("id")).toString();
            connect(cardBtn, &QPushButton::clicked, ov,
                    [this, ov, note, toId, gid, status]() {
                status->setText(QStringLiteral("Отправка…"));
                status->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
                api_->giftSend(gid, toId, note->text().trimmed(), false);
            });
            g->addWidget(cardBtn, i / 4, i % 4);
        }
        gridWrap->adjustSize();
    });
    connect(api_, &ApiClient::giftSent, ov, [this, ov, status](bool ok, const QString& msg) {
        if (ok) {
            ov->closeAnimated();
            afterAction();   // счётчик подарков зависит от сервера
            return;
        }
        status->setText(msg.isEmpty() ? QStringLiteral("Не удалось отправить") : msg);
        status->setStyleSheet(QStringLiteral("color:#E26A63;font-size:12px;"));
    });

    ov->showAnimated();
    api_->giftsCatalog();
}
