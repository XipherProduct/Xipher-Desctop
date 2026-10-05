#pragma once
#include <QColor>
#include <QList>
#include <QString>
#include "net/Prefs.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Палитра и стили взяты 1:1 из веб-клиента (web/css/login.css, web/index.html).
//  Здесь — единое место правды по цветам и глобальный QSS.
// ─────────────────────────────────────────────────────────────────────────────
namespace Theme {

// Фон/поверхности (из login.css и index.html токенов)
inline const QColor BgBase       {0x05, 0x06, 0x0f};          // #05060f
inline const QColor GlowPurple   {0x58, 0x50, 0xdc};          // rgba(88,80,220,*)
inline const QColor GlowBlue     {0x00, 0xa8, 0xff};          // rgba(0,168,255,*)

// Текст
inline const QColor TextMain     {0xf8, 0xfa, 0xfc};          // #f8fafc
inline const QColor TextSoft     {0xcb, 0xd5, 0xf5};          // #cbd5f5
inline const QColor TextMuted    {0x9a, 0xa4, 0xc6};          // #9aa4c6
inline const QColor LinkBlue     {0x60, 0xa5, 0xfa};          // #60a5fa
inline const QColor ErrorRed     {0xf8, 0x71, 0x71};          // #f87171
inline const QColor SuccessGreen {0x34, 0xd3, 0x99};          // #34d399

// Акценты бренда
inline const QColor Violet       {0x8b, 0x5c, 0xf6};          // #8B5CF6

// Глобальный стиль приложения. Селекторы по objectName (#id) и классам свойств.
inline QString styleSheet() {
    return QStringLiteral(R"QSS(
/* ─── Базовый текст ─── */
QWidget {
    color: #f8fafc;
    font-family: "Inter", "Segoe UI", system-ui, sans-serif;
    font-size: 14px;
}

/* ─── Карточка входа/регистрации (login.css .login-card) ─── */
#authCard {
    background: rgba(16,18,30,0.85);
    border: 1px solid rgba(255,255,255,0.06);
    border-radius: 26px;
}

/* ─── Бренд ─── */
#brandIcon {
    border-radius: 12px;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
        stop:0 #2563eb, stop:0.5 #8B5CF6, stop:1 #6D28D9);
    color: #ffffff;
    font-weight: 800;
    font-size: 20px;
}
#brandName {
    font-weight: 800;
    font-size: 20px;
    color: #ffffff;
    letter-spacing: 0.3px;
}
#brandTag {
    color: #a78bfa;
    font-weight: 700;
    font-size: 11px;
    letter-spacing: 1.5px;
}

/* ─── Заголовки карточки ─── */
#authHeading {
    font-size: 30px;
    font-weight: 800;
    color: #ffffff;
}
#authSub {
    font-size: 15px;
    color: #cbd5f5;
}

/* ─── Метки полей ─── */
.formLabel {
    font-weight: 600;
    color: #e5e7ff;
    font-size: 14px;
}

/* ─── Поля ввода (login.css .login-input) ─── */
QLineEdit.loginInput {
    background: rgba(255,255,255,0.04);
    border: 1px solid rgba(255,255,255,0.08);
    border-radius: 14px;
    min-height: 54px;
    padding: 0 16px;
    color: #f8fafc;
    font-size: 15px;
    selection-background-color: #8B5CF6;
}
QLineEdit.loginInput:focus {
    background: rgba(255,255,255,0.08);
    border: 1px solid #8b5cf6;
}
QLineEdit.loginInput::placeholder {
    color: #94a3b8;
}

/* ─── Кнопка-сабмит (login.css .login-submit) ─── */
QPushButton#submitBtn {
    min-height: 56px;
    border-radius: 14px;
    border: none;
    font-weight: 700;
    font-size: 16px;
    color: #ffffff;
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
        stop:0 #2563eb, stop:0.35 #3b82f6, stop:0.75 #8B5CF6, stop:1 #6D28D9);
}
QPushButton#submitBtn:hover {
    background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
        stop:0 #2f6bf0, stop:0.35 #4b8bf7, stop:0.75 #9b6dff, stop:1 #7c34e8);
}
QPushButton#submitBtn:disabled {
    color: rgba(255,255,255,0.55);
    background: rgba(255,255,255,0.06);
}

/* ─── Ссылки ─── */
QPushButton.linkPrimary {
    border: none;
    background: transparent;
    color: #60a5fa;
    font-weight: 700;
    font-size: 14px;
}
QPushButton.linkPrimary:hover { color: #93c5fd; }

QPushButton.linkSecondary {
    border: none;
    background: transparent;
    color: #8b5cf6;
    font-size: 13px;
}
QPushButton.linkSecondary:hover { color: #a78bfa; }

/* ─── Сообщения об ошибке/успехе под полями ─── */
.formError   { color: #f87171; font-size: 13px; }
.formSuccess { color: #34d399; font-size: 13px; }

/* ─── Текст условий ─── */
#authTerms { color: #9aa4c6; font-size: 13px; }
)QSS");
}


// ─────────────────────────────────────────────────────────────────────────────
//  Темы оформления (вкладка «Оформление» в настройках; токены из tokens.css).
//  Пресеты различаются фонами/поверхностями/акцентом. Выбор хранится в Prefs
//  (xipher_theme) и применяется мгновенно: ChatPage/SettingsDialog
//  перегенерируют свои QSS от токенов пресета.
// ─────────────────────────────────────────────────────────────────────────────
} // namespace Theme

namespace ThemePreset {

// Полный токен-набор, 1:1 с web/css/tokens.css. Значения пресетов сняты
// с каждого data-theme-блока веба; всё, чего в блоке нет, наследуется от
// серого дефолта — как каскад CSS.
struct Tokens {
    QString id;
    QString name;

    // Фон и поверхности
    QColor  bgBase;        // --bg-base
    QColor  surface1;      // --surface-1 (сайдбар, шапки)
    QColor  surface2;      // --surface-2 (поля, бабблы-in, пилюля)
    QColor  surface3;      // --surface-3 (ховеры)
    QColor  surface4;      // --surface-4 (press)
    QColor  surfaceInset;  // --surface-inset

    // Границы (hairline)
    QColor  borderSubtle;  // rgba(...,0.055..0.07)
    QColor  borderDefault; // rgba(...,0.10..0.12)
    QColor  borderStrong;  // rgba(...,0.16..0.18)

    // Текст
    QColor  textPrimary;   // #F3F1F8
    QColor  textSecondary; // #ACA6BD
    QColor  textTertiary;  // #726C82
    QColor  textDisabled;  // #4A4656

    // Акцент (матовый)
    QColor  accent;        // #8B5CF6
    QColor  accentHover;   // #9B72F8
    QColor  accentPressed; // #7A4AE6
    QColor  accentDeep;    // #6D28D9
    QColor  accentText;    // #BBA4FF (текст/ссылки на акценте)

    // Бабблы
    QColor  bubbleIn;      // = surface2
    QColor  bubbleOutA;    // градиент start (#4A3A72)
    QColor  bubbleOutB;    // градиент end   (#3A2D5C)
    QColor  bubbleOutSolid;// #41336A
    QColor  bubbleOutText; // #F0ECFA
    QColor  bubbleOutMeta; // rgba(240,236,250,0.8)

    // Семантика
    QColor  success;       // #46B98A
    QColor  warning;       // #D9A05B
    QColor  danger;        // #E26A63

    bool    available;
    bool    isLight = false;   // светлая тема: текст тёмный, тени мягче

    // rgba-строка от акцента: «rgba(139,92,246,0.14)» — для QSS-подстановок.
    QString accentRgba(qreal alpha) const {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(accent.red()).arg(accent.green()).arg(accent.blue()).arg(alpha);
    }
    QString rgba(const QColor& c, qreal alpha) const {
        return QStringLiteral("rgba(%1,%2,%3,%4)")
            .arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
    }
};

inline QList<Tokens> all() {
    // gray — дефолт :root; остальные пресеты меняют только свои токены,
    // остальное наследуют от него (как каскад в tokens.css).
    Tokens gray;
    gray.id = QStringLiteral("gray");   gray.name = QStringLiteral("Стандартная");
    gray.bgBase = {0x0B,0x0A,0x0E}; gray.surface1 = {0x13,0x12,0x18};
    gray.surface2 = {0x1A,0x18,0x22}; gray.surface3 = {0x22,0x1F,0x2C};
    gray.surface4 = {0x2B,0x27,0x37}; gray.surfaceInset = {0x10,0x0F,0x15};
    gray.borderSubtle  = {45, 45, 50, 14};   // ≈ rgba(255,255,255,0.055)
    gray.borderDefault = {45, 45, 50, 26};   // ≈ rgba(255,255,255,0.10)
    gray.borderStrong  = {45, 45, 50, 41};   // ≈ rgba(255,255,255,0.16)
    gray.textPrimary = {0xF3,0xF1,0xF8}; gray.textSecondary = {0xAC,0xA6,0xBD};
    gray.textTertiary = {0x72,0x6C,0x82}; gray.textDisabled = {0x4A,0x46,0x56};
    gray.accent = {0x8B,0x5C,0xF6}; gray.accentHover = {0x9B,0x72,0xF8};
    gray.accentPressed = {0x7A,0x4A,0xE6}; gray.accentDeep = {0x6D,0x28,0xD9};
    gray.accentText = {0xBB,0xA4,0xFF};
    gray.bubbleIn = gray.surface2;
    gray.bubbleOutA = {0x4A,0x3A,0x72}; gray.bubbleOutB = {0x3A,0x2D,0x5C};
    gray.bubbleOutSolid = {0x41,0x33,0x6A}; gray.bubbleOutText = {0xF0,0xEC,0xFA};
    gray.bubbleOutMeta = {240, 236, 250, 204};
    gray.success = {0x46,0xB9,0x8A}; gray.warning = {0xD9,0xA0,0x5B};
    gray.danger = {0xE2,0x6A,0x63};
    gray.available = true;

    auto preset = [](Tokens t, const char* id, const char* name, bool avail) {
        t.id = QString::fromLatin1(id); t.name = QString::fromUtf8(name);
        t.available = avail; return t;
    };

    // dark — глубже и холоднее серого
    Tokens dark = gray;
    dark.bgBase = {0x06,0x06,0x0A}; dark.surface1 = {0x0D,0x0D,0x12};
    dark.surface2 = {0x14,0x14,0x1B}; dark.surface3 = {0x1C,0x1C,0x25};
    dark.surface4 = {0x25,0x25,0x2F}; dark.surfaceInset = {0x0A,0x0A,0x0F};

    // amoled — чистый чёрный
    Tokens amoled = gray;
    amoled.bgBase = {0x00,0x00,0x00}; amoled.surface1 = {0x08,0x08,0x0A};
    amoled.surface2 = {0x0F,0x0F,0x12}; amoled.surface3 = {0x16,0x16,0x1A};
    amoled.surface4 = {0x1E,0x1E,0x24}; amoled.surfaceInset = {0x05,0x05,0x06};
    amoled.borderSubtle = {255,255,255,18}; amoled.borderDefault = {255,255,255,31};
    amoled.borderStrong = {255,255,255,46};

    // purple — насыщенно-фиолетовая
    Tokens purple = gray;
    purple.bgBase = {0x0E,0x0A,0x18}; purple.surface1 = {0x17,0x10,0x26};
    purple.surface2 = {0x1F,0x16,0x33}; purple.surface3 = {0x29,0x1E,0x45};
    purple.surface4 = {0x34,0x27,0x55}; purple.surfaceInset = {0x12,0x0C,0x20};
    purple.accent = {0xA7,0x8B,0xFA}; purple.accentHover = {0xB9,0xA2,0xFC};
    purple.accentPressed = {0x8B,0x5C,0xF6}; purple.accentDeep = {0x7C,0x3A,0xED};
    purple.accentText = {0xD6,0xC8,0xFF};
    purple.bubbleIn = purple.surface2;
    purple.bubbleOutA = {0x5B,0x3F,0xA0}; purple.bubbleOutB = {0x46,0x32,0x7F};
    purple.bubbleOutSolid = {0x4F,0x39,0x90}; purple.bubbleOutText = {0xF2,0xEC,0xFF};
    purple.bubbleOutMeta = {242, 236, 255, 204};

    // blue — серый dark base + синий акцент
    Tokens blue = gray;
    blue.accent = {0x3B,0x82,0xF6}; blue.accentHover = {0x60,0xA5,0xFA};
    blue.accentPressed = {0x25,0x63,0xEB}; blue.accentDeep = {0x1D,0x4E,0xD8};
    blue.accentText = {0x93,0xC5,0xFD};
    blue.bubbleOutA = {0x27,0x40,0x6E}; blue.bubbleOutB = {0x1E,0x33,0x58};
    blue.bubbleOutSolid = {0x24,0x3B,0x63}; blue.bubbleOutText = {0xEA,0xF2,0xFF};
    blue.bubbleOutMeta = {234, 242, 255, 204};

    // green — изумрудный акцент
    Tokens green = gray;
    green.accent = {0x10,0xB9,0x81}; green.accentHover = {0x34,0xD3,0x99};
    green.accentPressed = {0x05,0x96,0x69}; green.accentDeep = {0x04,0x78,0x57};
    green.accentText = {0x6E,0xE7,0xB7};
    green.bubbleOutA = {0x17,0x53,0x3F}; green.bubbleOutB = {0x11,0x43,0x34};
    green.bubbleOutSolid = {0x16,0x4E,0x3C}; green.bubbleOutText = {0xE8,0xFB,0xF3};
    green.bubbleOutMeta = {232, 251, 243, 204};

    // mocha — тёплая «нюд/беж» с песочно-золотым акцентом
    Tokens mocha = gray;
    mocha.bgBase = {0x0F,0x0C,0x0A}; mocha.surface1 = {0x1A,0x15,0x12};
    mocha.surface2 = {0x22,0x1B,0x16}; mocha.surface3 = {0x2C,0x23,0x1D};
    mocha.surface4 = {0x37,0x2C,0x24}; mocha.surfaceInset = {0x14,0x0F,0x0C};
    mocha.borderSubtle = {255,240,225,15}; mocha.borderDefault = {255,240,225,26};
    mocha.borderStrong = {255,240,225,41};
    mocha.textPrimary = {0xF4,0xED,0xE4}; mocha.textSecondary = {0xC2,0xB4,0xA2};
    mocha.textTertiary = {0x8A,0x7C,0x6B}; mocha.textDisabled = {0x5A,0x4E,0x40};
    mocha.accent = {0xD9,0xA8,0x65}; mocha.accentHover = {0xE6,0xBC,0x82};
    mocha.accentPressed = {0xC2,0x92,0x4E}; mocha.accentDeep = {0xA8,0x79,0x38};
    mocha.accentText = {0xF0,0xD6,0xAC};
    mocha.bubbleIn = mocha.surface2;
    mocha.bubbleOutA = {0x5C,0x45,0x28}; mocha.bubbleOutB = {0x47,0x34,0x20};
    mocha.bubbleOutSolid = {0x52,0x3E,0x26}; mocha.bubbleOutText = {0xF8,0xEE,0xDF};
    mocha.bubbleOutMeta = {248, 238, 223, 204};

    // light — единственная светлая
    Tokens light = gray;
    light.bgBase = {0xF6,0xF4,0xFB}; light.surface1 = {0xFF,0xFF,0xFF};
    light.surface2 = {0xF3,0xF1,0xF9}; light.surface3 = {0xEC,0xE9,0xF4};
    light.surface4 = {0xE2,0xDE,0xEE}; light.surfaceInset = {0xF0,0xED,0xF8};
    light.borderSubtle = {20,16,40,15}; light.borderDefault = {20,16,40,26};
    light.borderStrong = {20,16,40,46};
    light.textPrimary = {0x1A,0x18,0x20}; light.textSecondary = {0x5C,0x56,0x70};
    light.textTertiary = {0x8C,0x86,0xA0}; light.textDisabled = {0xB5,0xB0,0xC4};
    light.accent = {0x7C,0x4A,0xE6}; light.accentHover = {0x8B,0x5C,0xF6};
    light.accentPressed = {0x6B,0x3A,0xD4}; light.accentDeep = {0x6D,0x28,0xD9};
    light.accentText = {0x6D,0x28,0xD9};
    light.bubbleIn = {0xFF,0xFF,0xFF};
    light.bubbleOutA = {0xE4,0xDA,0xFB}; light.bubbleOutB = {0xD6,0xC8,0xF5};
    light.bubbleOutSolid = {0xDD,0xD1,0xF8}; light.bubbleOutText = {0x2A,0x20,0x40};
    light.bubbleOutMeta = {42, 32, 64, 204};
    light.isLight = true;

    return {
        preset(gray,   "gray",   "Стандартная", true),
        preset(dark,   "dark",   "Тёмная",      true),
        preset(amoled, "amoled", "AMOLED",      true),
        preset(purple, "purple", "Фиолетовая",  true),
        preset(blue,   "blue",   "Синяя",       true),
        preset(green,  "green",  "Изумрудная",  true),
        preset(mocha,  "mocha",  "Мокко",       true),
        preset(light,  "light",  "Светлая",     true),
    };
}

inline Tokens current() {
    const QString saved = Prefs::getStr(QStringLiteral("xipher_theme"), QStringLiteral("gray"));
    for (const Tokens& t : all())
        if (t.id == saved && t.available) return t;
    return all().first();
}

inline void save(const QString& id) { Prefs::setStr(QStringLiteral("xipher_theme"), id); }

// Подстановка @{токен}-плейсхолдеров в QSS-литерал активной темой.
// Словарь: s1..s4, inset, bg, ac/acH/acP/acD/acT, tp/ts/tt/td,
// bSub/bDef/bStr, soft1..soft4/ac10..ac34, hovW, scrol/scrolH,
// gradA/gradB (баббл-out), ok/warn/danger.
// Соглашение то же, что в ChatPage::chatQSS — один дизайн-язык на всё приложение.
inline QString applyTokens(const QString& qss) {
    const Tokens th = current();
    const bool light = th.isLight;
    struct Sub { const char* key; QString val; };
    const Sub subs[] = {
        {"@{s1}",    th.surface1.name()},
        {"@{s2}",    th.surface2.name()},
        {"@{s3}",    th.surface3.name()},
        {"@{s4}",    th.surface4.name()},
        {"@{inset}", th.surfaceInset.name()},
        {"@{bg}",    th.bgBase.name()},
        {"@{ac}",    th.accent.name()},
        {"@{acH}",   th.accentHover.name()},
        {"@{acP}",   th.accentPressed.name()},
        {"@{acD}",   th.accentDeep.name()},
        {"@{acT}",   th.accentText.name()},
        {"@{tp}",    th.textPrimary.name()},
        {"@{ts}",    th.textSecondary.name()},
        {"@{tt}",    th.textTertiary.name()},
        {"@{td}",    th.textDisabled.name()},
        {"@{bSub}",  th.rgba(th.borderSubtle, 1.0)},
        {"@{bDef}",  th.rgba(th.borderDefault, 1.0)},
        {"@{bStr}",  th.rgba(th.borderStrong, 1.0)},
        {"@{soft1}", th.accentRgba(0.14)},
        {"@{soft2}", th.accentRgba(0.22)},
        {"@{soft3}", th.accentRgba(0.10)},
        {"@{soft4}", th.accentRgba(0.28)},
        {"@{ac10}",  th.accentRgba(0.10)},
        {"@{ac14}",  th.accentRgba(0.14)},
        {"@{ac22}",  th.accentRgba(0.22)},
        {"@{ac34}",  th.accentRgba(0.34)},
        {"@{hovW}",  th.rgba(QColor(255, 255, 255), light ? 0.45 : 0.06)},
        {"@{hovW2}", th.rgba(QColor(255, 255, 255), light ? 0.60 : 0.10)},
        {"@{scrol}", th.rgba(QColor(255, 255, 255), light ? 0.28 : 0.12)},
        {"@{scrolH}",th.rgba(QColor(255, 255, 255), light ? 0.50 : 0.22)},
        {"@{gradA}", th.bubbleOutA.name()},
        {"@{gradB}", th.bubbleOutB.name()},
        {"@{ok}",    th.success.name()},
        {"@{warn}",  th.warning.name()},
        {"@{danger}",th.danger.name()},
    };
    QString out = qss;
    for (const Sub& s : subs) out.replace(QLatin1String(s.key), s.val);
    return out;
}

} // namespace ThemePreset
