#include "ui/DownloadBar.h"
#include "net/DownloadCenter.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimerEvent>

namespace {
QString human(qint64 b) {
    if (b < 1024) return QStringLiteral("%1 Б").arg(b);
    if (b < 1048576) return QStringLiteral("%1 КБ").arg(b / 1024);
    if (b < 1073741824LL) return QStringLiteral("%1 МБ").arg(QString::number(b / 1048576.0, 'f', 1));
    return QStringLiteral("%1 ГБ").arg(QString::number(b / 1073741824.0, 'f', 2));
}
}

DownloadBar::DownloadBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("downloadBar"));
    setStyleSheet(QStringLiteral(
        "#downloadBar{background:#16141D;border-top:1px solid rgba(255,255,255,0.07);}"
        "#dlIcon{color:#8B5CF6;font-size:14px;}"
        "#dlText{color:#F3F1F8;font-size:12px;}"
        "#dlCancel{background:transparent;border:none;color:#726C82;font-size:13px;padding:0 6px;}"
        "#dlCancel:hover{color:#F3F1F8;}"));
    auto* l = new QHBoxLayout(this);
    l->setContentsMargins(14, 6, 10, 6);
    l->setSpacing(8);
    auto* icon = new QLabel(QStringLiteral("⬇"), this);
    icon->setObjectName(QStringLiteral("dlIcon"));
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("dlText"));
    cancelBtn_ = new QPushButton(QStringLiteral("✕"), this);
    cancelBtn_->setObjectName(QStringLiteral("dlCancel"));
    cancelBtn_->setFixedSize(24, 24);
    cancelBtn_->setCursor(Qt::PointingHandCursor);
    l->addWidget(icon);
    l->addWidget(text_, 1);
    l->addWidget(cancelBtn_);
    connect(cancelBtn_, &QPushButton::clicked, this, [this] {
        const QString p = DownloadCenter::instance().activePath();
        if (!p.isEmpty()) emit cancelRequested(p);
    });
    connect(&DownloadCenter::instance(), &DownloadCenter::changed,
            this, &DownloadBar::refresh, Qt::QueuedConnection);
    hide();
    startTimer(500);   // тик для скорости
}

void DownloadBar::timerEvent(QTimerEvent* e) {
    if (e->timerId() == 0 && isVisible()) refresh();
    QWidget::timerEvent(e);
}

void DownloadBar::refresh() {
    auto& dc = DownloadCenter::instance();
    if (!dc.hasActive()) { hide(); return; }
    show();
    raise();
    QString speed;
    const qint64 bps = dc.activeSpeedBps();
    if (bps > 0) speed = QStringLiteral(" · %1/с").arg(human(bps));
    text_->setText(QStringLiteral("⬇ %1  ·  %2 из %3  (%4%)%5")
        .arg(dc.activeName(),
             human(dc.activeReceived()),
             human(dc.activeTotal()),
             QString::number(dc.activePercent()),
             speed));
}
