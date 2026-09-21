#pragma once
#include "ui/Theme.h"

#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QAction>
#include <QIcon>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGraphicsDropShadowEffect>

// ─────────────────────────────────────────────────────────────────────────────
//  Мелкие хелперы построения карточки входа/регистрации. Inline — общие для
//  LoginPage и RegisterPage, чтобы вёрстка совпадала 1:1.
// ─────────────────────────────────────────────────────────────────────────────
namespace AuthUi {

// Карточка #authCard с фиолетовым свечением (как box-shadow в login.css).
inline QFrame* makeCard(QWidget* parent) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("authCard"));
    card->setFixedWidth(520);

    auto* glow = new QGraphicsDropShadowEffect(card);
    glow->setBlurRadius(80);
    glow->setOffset(0, 18);
    QColor c = Theme::GlowPurple; c.setAlphaF(0.35);
    glow->setColor(c);
    card->setGraphicsEffect(glow);
    return card;
}

// Шапка с брендом: иконка "X" + "Xipher" + тег "DESKTOP" (наша инд. штучка).
inline QWidget* makeBrand(QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(12);

    auto* icon = new QLabel(QStringLiteral("X"), row);
    icon->setObjectName(QStringLiteral("brandIcon"));
    icon->setFixedSize(46, 46);
    icon->setAlignment(Qt::AlignCenter);

    auto* name = new QLabel(QStringLiteral("Xipher"), row);
    name->setObjectName(QStringLiteral("brandName"));

    auto* tag = new QLabel(QStringLiteral("DESKTOP"), row);
    tag->setObjectName(QStringLiteral("brandTag"));
    tag->setAlignment(Qt::AlignBottom);

    lay->addWidget(icon);
    lay->addWidget(name);
    lay->addWidget(tag);
    lay->addStretch();
    return row;
}

inline QLabel* makeHeading(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("authHeading"));
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignHCenter);
    return l;
}

inline QLabel* makeSub(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("authSub"));
    l->setWordWrap(true);
    l->setAlignment(Qt::AlignHCenter);
    return l;
}

inline QLabel* makeFieldLabel(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("class", "formLabel");
    return l;
}

inline QLineEdit* makeInput(const QString& placeholder, QWidget* parent, bool password = false) {
    auto* e = new QLineEdit(parent);
    e->setProperty("class", "loginInput");
    e->setPlaceholderText(placeholder);
    e->setClearButtonEnabled(false);
    if (password) {
        e->setEchoMode(QLineEdit::Password);
        // Глазик: показать/скрыть пароль. Клик по иконке в правом краю поля
        // переключает режим; подсказка меняется, фокус не крадётся.
        auto* action = new QAction(e);
        action->setIcon(QIcon(QStringLiteral(":/icons/eye-open.svg")));
        action->setToolTip(QStringLiteral("Показать пароль"));
        e->addAction(action, QLineEdit::TrailingPosition);
        QObject::connect(action, &QAction::triggered, action, [e, action]() {
            const bool hidden = e->echoMode() == QLineEdit::Password;
            e->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
            action->setIcon(QIcon(hidden ? QStringLiteral(":/icons/eye-closed.svg")
                                         : QStringLiteral(":/icons/eye-open.svg")));
            action->setToolTip(hidden ? QStringLiteral("Скрыть пароль")
                                      : QStringLiteral("Показать пароль"));
            e->setFocus(Qt::OtherFocusReason);
        });
    }
    return e;
}

inline QLabel* makeErrorLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setProperty("class", "formError");
    l->setWordWrap(true);
    l->hide();
    return l;
}

inline QLabel* makeSuccessLabel(QWidget* parent) {
    auto* l = new QLabel(parent);
    l->setProperty("class", "formSuccess");
    l->setWordWrap(true);
    l->hide();
    return l;
}

inline void showError(QLabel* label, const QString& text) {
    if (!label) return;
    label->setText(text);
    label->setVisible(!text.isEmpty());
}

} // namespace AuthUi
