#include "ui/RecordingBar.h"
#include "ui/Icons.h"

#include <QPainter>
#include <QTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QtMath>

// ── Пульсирующая красная точка ────────────────────────────────────────────────
class PulseDot : public QWidget {
public:
    explicit PulseDot(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(16, 16);
        timer_ = new QTimer(this);
        timer_->setInterval(40);
        connect(timer_, &QTimer::timeout, this, [this]() { phase_ += 0.12; update(); });
    }
    void startAnim() { timer_->start(); }
    void stopAnim()  { timer_->stop(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal t = (qSin(phase_) + 1.0) / 2.0;       // 0..1
        const qreal r = 4.0 + t * 2.0;
        const QPointF c(width() / 2.0, height() / 2.0);
        // мягкое свечение
        QColor glow(0xE2, 0x6A, 0x63, int(60 + t * 70));
        p.setPen(Qt::NoPen);
        p.setBrush(glow);
        p.drawEllipse(c, r + 4, r + 4);
        p.setBrush(QColor(0xE2, 0x6A, 0x63));
        p.drawEllipse(c, r, r);
    }

private:
    QTimer* timer_;
    qreal phase_ = 0.0;
};

// ── Живой waveform — РЕАЛЬНЫЙ уровень микрофона ───────────────────────────────
//  pushLevel(qreal) кормит историю из готовых чанков PCM (VoiceRecorder::inputLevel,
//  RMS → dB-нормировка). Рисуем скроллящуюся историю: штрих = уровень среза,
//  старое уезжает влево — как в Telegram/Discord. Никаких синусоид: волна
//  молчит в тишине и прыгает от голоса.
class LiveWave : public QWidget {
public:
    explicit LiveWave(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(30);
    }
    void pushLevel(qreal lvl) {
        levels_.append(qBound(0.0, lvl, 1.0));
        while (levels_.size() > kBars) levels_.removeFirst();
        update();
    }
    void reset() { levels_.clear(); update(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int n = kBars;
        const qreal w = width();
        const qreal hgt = height();
        const qreal slot = w / n;
        const qreal barW = qMax<qreal>(2.0, slot - 3.0);
        const qreal maxH = hgt - 6.0;
        for (int i = 0; i < n; ++i) {
            // уровень i-го среза; новейшие справа, недостающие — минимум
            const int idx = levels_.size() - n + i;
            const qreal lvl = idx >= 0 ? levels_[idx] : 0.0;
            const qreal bh = qMax<qreal>(3.0, (0.06 + 0.94 * lvl) * maxH);
            const qreal x = i * slot + (slot - barW) / 2.0;
            const qreal y = (hgt - bh) / 2.0;
            qreal centerFade = 1.0 - qAbs(i - n / 2.0) / (n / 2.0);
            int alpha = int(120 + 110 * centerFade);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xBB, 0xA4, 0xFF, alpha));
            p.drawRoundedRect(QRectF(x, y, barW, bh), barW / 2.0, barW / 2.0);
        }
    }

private:
    static constexpr int kBars = 40;
    QList<qreal> levels_;
};

// ── RecordingBar ──────────────────────────────────────────────────────────────

RecordingBar::RecordingBar(QWidget* parent) : QWidget(parent) {
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(14, 10, 14, 10);
    lay->setSpacing(12);

    auto* cancel = new QPushButton(this);
    cancel->setCursor(Qt::PointingHandCursor);
    cancel->setFixedSize(40, 40);
    cancel->setIcon(Icons::icon(Icons::Trash, 18, QColor(0xE2, 0x6A, 0x63)));
    cancel->setIconSize(QSize(18, 18));
    cancel->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:20px;background:#1A1822;}"
        "QPushButton:hover{background:#2B1F22;}"));

    dot_ = new PulseDot(this);

    time_ = new QLabel(QStringLiteral("0:00"), this);
    time_->setStyleSheet(QStringLiteral("color:#F3F1F8;font-size:14px;font-weight:700;"));
    time_->setFixedWidth(44);

    wave_ = new LiveWave(this);

    auto* send = new QPushButton(this);
    send->setCursor(Qt::PointingHandCursor);
    send->setFixedSize(44, 44);
    send->setIcon(Icons::icon(Icons::Send, 20, QColor(0xFF, 0xFF, 0xFF)));
    send->setIconSize(QSize(20, 20));
    send->setStyleSheet(QStringLiteral(
        "QPushButton{border:none;border-radius:22px;"
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);}"
        "QPushButton:hover{background:#9B72F8;}"));

    lay->addWidget(cancel);
    lay->addWidget(dot_);
    lay->addWidget(time_);
    lay->addWidget(wave_, 1);
    lay->addWidget(send);

    secTimer_ = new QTimer(this);
    secTimer_->setInterval(1000);
    connect(secTimer_, &QTimer::timeout, this, [this]() {
        ++secs_;
        time_->setText(QStringLiteral("%1:%2").arg(secs_ / 60).arg(secs_ % 60, 2, 10, QLatin1Char('0')));
    });

    connect(cancel, &QPushButton::clicked, this, &RecordingBar::cancelClicked);
    connect(send,   &QPushButton::clicked, this, &RecordingBar::sendClicked);
}

void RecordingBar::start() {
    secs_ = 0;
    time_->setText(QStringLiteral("0:00"));
    secTimer_->start();
    dot_->startAnim();
}

void RecordingBar::stop() {
    secTimer_->stop();
    dot_->stopAnim();
    wave_->reset();   // новая запись — с чистой волной
}

void RecordingBar::pushLevel(qreal lvl) {
    wave_->pushLevel(lvl);
}
