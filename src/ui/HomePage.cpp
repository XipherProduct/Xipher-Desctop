#include "ui/HomePage.h"
#include "net/Session.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

HomePage::HomePage(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QStringLiteral("QWidget{background:#0b0d18;color:#f8fafc;}"));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(40, 40, 40, 40);
    lay->setSpacing(12);
    lay->addStretch();

    auto* title = new QLabel(QStringLiteral("✅ Вход выполнен"), this);
    title->setStyleSheet(QStringLiteral("font-size:28px;font-weight:800;"));
    title->setAlignment(Qt::AlignHCenter);

    who_ = new QLabel(this);
    who_->setStyleSheet(QStringLiteral("font-size:16px;color:#cbd5f5;"));
    who_->setAlignment(Qt::AlignHCenter);

    uid_ = new QLabel(this);
    uid_->setStyleSheet(QStringLiteral("font-size:13px;color:#9aa4c6;"));
    uid_->setAlignment(Qt::AlignHCenter);

    auto* hint = new QLabel(
        QStringLiteral("Это временный экран v0.1. Здесь будет полноценный мессенджер."), this);
    hint->setStyleSheet(QStringLiteral("font-size:13px;color:#55556a;"));
    hint->setAlignment(Qt::AlignHCenter);

    auto* logout = new QPushButton(QStringLiteral("Выйти"), this);
    logout->setCursor(Qt::PointingHandCursor);
    logout->setStyleSheet(QStringLiteral(
        "QPushButton{min-height:44px;border-radius:12px;border:1px solid rgba(255,255,255,12%);"
        "background:rgba(255,255,255,4%);color:#f8fafc;font-weight:600;padding:0 24px;}"
        "QPushButton:hover{background:rgba(255,255,255,8%);}"));

    lay->addWidget(title);
    lay->addSpacing(8);
    lay->addWidget(who_);
    lay->addWidget(uid_);
    lay->addSpacing(20);
    lay->addWidget(hint);
    lay->addSpacing(28);
    lay->addWidget(logout, 0, Qt::AlignHCenter);
    lay->addStretch();

    connect(logout, &QPushButton::clicked, this, &HomePage::logoutRequested);
}

void HomePage::refresh() {
    const Session& s = Session::instance();
    who_->setText(QStringLiteral("Вы вошли как: <b>%1</b>")
                      .arg(s.username.isEmpty() ? QStringLiteral("(без имени)") : s.username.toHtmlEscaped()));
    uid_->setText(QStringLiteral("user_id: %1").arg(s.userId.isEmpty() ? QStringLiteral("—") : s.userId));
}
