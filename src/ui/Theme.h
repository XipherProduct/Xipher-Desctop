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

struct Tokens {
    QString id;
    QString name;
    QColor  bgBase;      // --bg-base
    QColor  surface1;    // --surface-1 (сайдбар, шапки)
    QColor  surface2;    // --surface-2 (поля, бабблы-in, пилюля)
    QColor  surface3;    // --surface-3 (ховеры)
    QString accent;      // --accent (#rrggbb)
    int     accentR, accentG, accentB;
    bool    available;   // «Скоро»-пресеты недоступны
};

inline QList<Tokens> all() {
    return {
        {QStringLiteral("gray"),   QStringLiteral("Стандартная"), {0x0B,0x0A,0x0E}, {0x13,0x12,0x18}, {0x1A,0x18,0x22}, {0x22,0x1F,0x2C}, QStringLiteral("#8B5CF6"), 139, 92, 246, true},
        {QStringLiteral("dark"),   QStringLiteral("Тёмная"),      {0x06,0x06,0x0A}, {0x0D,0x0D,0x12}, {0x14,0x14,0x1B}, {0x1C,0x1C,0x25}, QStringLiteral("#8B5CF6"), 139, 92, 246, true},
        {QStringLiteral("amoled"), QStringLiteral("AMOLED"),      {0x00,0x00,0x00}, {0x08,0x08,0x0A}, {0x0F,0x0F,0x12}, {0x16,0x16,0x1A}, QStringLiteral("#8B5CF6"), 139, 92, 246, true},
        {QStringLiteral("purple"), QStringLiteral("Фиолетовая"),  {0x0E,0x0A,0x18}, {0x17,0x10,0x26}, {0x1F,0x16,0x33}, {0x29,0x1E,0x45}, QStringLiteral("#A78BFA"), 167, 139, 250, true},
        {QStringLiteral("blue"),   QStringLiteral("Синяя"),       {0x0B,0x0A,0x0E}, {0x13,0x12,0x18}, {0x1A,0x18,0x22}, {0x22,0x1F,0x2C}, QStringLiteral("#3B82F6"), 59, 130, 246, false},
        {QStringLiteral("green"),  QStringLiteral("Изумрудная"),  {0x0B,0x0A,0x0E}, {0x13,0x12,0x18}, {0x1A,0x18,0x22}, {0x22,0x1F,0x2C}, QStringLiteral("#10B981"), 16, 185, 129, false},
        {QStringLiteral("mocha"),  QStringLiteral("Мокко"),       {0x0F,0x0C,0x0A}, {0x1A,0x15,0x12}, {0x22,0x1B,0x16}, {0x2C,0x23,0x1D}, QStringLiteral("#D9A865"), 217, 168, 101, false},
        {QStringLiteral("light"),  QStringLiteral("Светлая"),     {0xF6,0xF4,0xFB}, {0xFF,0xFF,0xFF}, {0xF3,0xF1,0xF9}, {0xEC,0xE9,0xF4}, QStringLiteral("#7C4AE6"), 124, 74, 230, false},
    };
}

inline Tokens current() {
    const QString saved = Prefs::getStr(QStringLiteral("xipher_theme"), QStringLiteral("gray"));
    for (const Tokens& t : all())
        if (t.id == saved && t.available) return t;
    return all().first();
}

inline void save(const QString& id) { Prefs::setStr(QStringLiteral("xipher_theme"), id); }

} // namespace ThemePreset
