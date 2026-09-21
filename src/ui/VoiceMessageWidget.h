#pragma once
#include <QWidget>
#include <QVector>

class QLabel;
class PlayPauseButton;

// ─────────────────────────────────────────────────────────────────────────────
//  Waveform — дорожка-«осциллограмма» голосового (как в Telegram Desktop):
//  столбики разной высоты, проигранная часть подсвечена, клик = перемотка.
//  Форма волны детерминированно генерится из seed (id сообщения).
// ─────────────────────────────────────────────────────────────────────────────
class Waveform : public QWidget {
    Q_OBJECT
public:
    Waveform(const QString& seed, bool outgoing, QWidget* parent = nullptr);
    void setProgress(qreal p);   // 0..1

signals:
    void seek(qreal frac);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;

private:
    QVector<qreal> bars_;
    qreal progress_ = 0.0;
    bool  outgoing_;
};

// Голосовое сообщение (контент баббла): [▶/⏸][waveform][время].
class VoiceMessageWidget : public QWidget {
    Q_OBJECT
public:
    VoiceMessageWidget(const QString& seed, bool outgoing, QWidget* parent = nullptr);

    void setPlaying(bool playing);
    void setProgress(qreal frac);     // двигает заливку waveform
    void setElapsedMs(qint64 ms);
    void setUnavailable(const QString& reason);   // файл недоступен/ошибка декодера     // обновляет таймер
    void setTotalMs(qint64 ms);
    bool isPlaying() const { return playing_; }

signals:
    void playPauseClicked();
    void seekRequested(qreal frac);

private:
    QString fmt(qint64 ms) const;

    PlayPauseButton* playBtn_;
    Waveform*        wave_;
    QLabel*          time_;
    bool playing_ = false;
    bool outgoing_;
    qint64 totalMs_ = 0;
};
