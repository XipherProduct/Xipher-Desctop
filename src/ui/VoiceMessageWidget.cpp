#include "ui/VoiceMessageWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QLabel>

// ── PlayPauseButton — круглая кнопка с векторной иконкой play/pause ───────────
class PlayPauseButton : public QAbstractButton {
public:
    explicit PlayPauseButton(bool outgoing, QWidget* parent = nullptr)
        : QAbstractButton(parent), outgoing_(outgoing) {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
    }
    void setPlaying(bool playing) { playing_ = playing; update(); }

protected:
    void enterEvent(QEnterEvent*) override { hover_ = true; update(); }
    void leaveEvent(QEvent*) override { hover_ = false; update(); }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = rect().adjusted(1, 1, -1, -1);
        const QPointF c = r.center();
        const qreal s = qMin(r.width(), r.height());

        // Фон-кружок
        p.setPen(Qt::NoPen);
        if (outgoing_) {
            p.setBrush(hover_ ? QColor(0xF2, 0xEC, 0xFF) : QColor(0xFF, 0xFF, 0xFF));
        } else {
            QLinearGradient g(r.topLeft(), r.bottomRight());
            g.setColorAt(0, hover_ ? QColor(0x9B, 0x72, 0xF8) : QColor(0x8B, 0x5C, 0xF6));
            g.setColorAt(1, QColor(0x6D, 0x28, 0xD9));
            p.setBrush(g);
        }
        p.drawEllipse(r);

        // Иконка
        const QColor fg = outgoing_ ? QColor(0x5B, 0x3F, 0xA6) : QColor(0xFF, 0xFF, 0xFF);
        p.setBrush(fg);
        p.setPen(Qt::NoPen);
        if (playing_) {
            // Две скруглённые палочки (pause)
            const qreal bw = s * 0.12;
            const qreal bh = s * 0.34;
            const qreal gap = s * 0.10;
            p.drawRoundedRect(QRectF(c.x() - gap - bw, c.y() - bh / 2, bw, bh), bw / 2, bw / 2);
            p.drawRoundedRect(QRectF(c.x() + gap,      c.y() - bh / 2, bw, bh), bw / 2, bw / 2);
        } else {
            // Скруглённый треугольник (play), слегка смещён вправо для оптического центра
            const qreal ts = s * 0.30;
            QPainterPath tri;
            tri.moveTo(c.x() - ts * 0.55 + s * 0.04, c.y() - ts);
            tri.lineTo(c.x() + ts * 0.95 + s * 0.04, c.y());
            tri.lineTo(c.x() - ts * 0.55 + s * 0.04, c.y() + ts);
            tri.closeSubpath();
            QPen pen(fg);
            pen.setWidthF(s * 0.10);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            p.drawPath(tri);
        }
    }

private:
    bool outgoing_;
    bool playing_ = false;
    bool hover_ = false;
};

// ── Waveform ──────────────────────────────────────────────────────────────────

Waveform::Waveform(const QString& seed, bool outgoing, QWidget* parent)
    : QWidget(parent), outgoing_(outgoing) {
    setMinimumWidth(140);
    setMinimumHeight(28);
    setCursor(Qt::PointingHandCursor);

    // Детерминированная «осциллограмма» из seed (как в TG — без реального декода).
    quint32 h = qHash(seed.isEmpty() ? QStringLiteral("xipher") : seed);
    auto rnd = [&h]() {
        h = h * 1664525u + 1013904223u;          // LCG
        return (h >> 8 & 0xFFFF) / 65535.0;
    };
    const int N = 48;
    bars_.reserve(N);
    qreal prev = 0.5;
    for (int i = 0; i < N; ++i) {
        // плавно меняющаяся высота 0.12..1.0
        qreal target = 0.12 + rnd() * 0.88;
        prev = prev * 0.45 + target * 0.55;
        bars_.append(prev);
    }
}

void Waveform::setProgress(qreal p) {
    progress_ = qBound(0.0, p, 1.0);
    update();
}

void Waveform::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int n = bars_.size();
    if (n == 0) return;
    const qreal w = width();
    const qreal hgt = height();
    const qreal slot = w / n;
    const qreal barW = qMax<qreal>(2.0, slot - 2.0);
    const qreal maxH = hgt - 4.0;

    const QColor played   = outgoing_ ? QColor(0xFF, 0xFF, 0xFF)
                                      : QColor(0x8B, 0x5C, 0xF6);
    const QColor unplayed = outgoing_ ? QColor(255, 255, 255, 100)
                                      : QColor(255, 255, 255, 55);

    const qreal playX = progress_ * w;
    for (int i = 0; i < n; ++i) {
        const qreal bh = qMax<qreal>(3.0, bars_[i] * maxH);
        const qreal x = i * slot + (slot - barW) / 2.0;
        const qreal y = (hgt - bh) / 2.0;
        QRectF r(x, y, barW, bh);
        p.setPen(Qt::NoPen);
        p.setBrush((x + barW / 2.0) <= playX ? played : unplayed);
        p.drawRoundedRect(r, barW / 2.0, barW / 2.0);
    }
}

void Waveform::mousePressEvent(QMouseEvent* e) {
    if (width() > 0) emit seek(qBound(0.0, e->position().x() / width(), 1.0));
}

// ── VoiceMessageWidget ────────────────────────────────────────────────────────

VoiceMessageWidget::VoiceMessageWidget(const QString& seed, bool outgoing, QWidget* parent)
    : QWidget(parent), outgoing_(outgoing) {
    setAttribute(Qt::WA_StyledBackground, false);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 2, 0, 2);
    lay->setSpacing(10);

    playBtn_ = new PlayPauseButton(outgoing_, this);
    playBtn_->setFixedSize(38, 38);

    wave_ = new Waveform(seed, outgoing_, this);

    time_ = new QLabel(QStringLiteral("0:00"), this);
    time_->setStyleSheet(QString("color:%1;font-size:12px;")
        .arg(outgoing_ ? QStringLiteral("rgba(240,236,250,0.75)") : QStringLiteral("#ACA6BD")));

    lay->addWidget(playBtn_);
    lay->addWidget(wave_, 1);
    lay->addWidget(time_);

    connect(playBtn_, &QAbstractButton::clicked, this, &VoiceMessageWidget::playPauseClicked);
    connect(wave_, &Waveform::seek, this, &VoiceMessageWidget::seekRequested);
}

void VoiceMessageWidget::setPlaying(bool playing) {
    playing_ = playing;
    playBtn_->setPlaying(playing);
}

void VoiceMessageWidget::setProgress(qreal frac) {
    wave_->setProgress(frac);
}

QString VoiceMessageWidget::fmt(qint64 ms) const {
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

void VoiceMessageWidget::setElapsedMs(qint64 ms) {
    time_->setText(fmt(ms));
}

void VoiceMessageWidget::setUnavailable(const QString& reason) {
    setPlaying(false);
    setProgress(0.0);
    time_->setText(QStringLiteral("—"));
    setToolTip(QStringLiteral("%1\nФайл больше не доступен на сервере.").arg(reason));
    setEnabled(false);   // визуально гасим, история остаётся
}

void VoiceMessageWidget::setTotalMs(qint64 ms) {
    totalMs_ = ms;
    if (!playing_) time_->setText(fmt(ms));
}
