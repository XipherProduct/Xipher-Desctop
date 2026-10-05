#pragma once
#include <QWidget>

class QLabel;
class QTimer;
class PulseDot;
class LiveWave;

// ─────────────────────────────────────────────────────────────────────────────
//  RecordingBar — панель записи голосового в стиле Discord:
//  пульсирующая красная точка, таймер, «живой» анимированный waveform и
//  кнопки отмены/отправки.
// ─────────────────────────────────────────────────────────────────────────────
class RecordingBar : public QWidget {
    Q_OBJECT
public:
    explicit RecordingBar(QWidget* parent = nullptr);

    void start();   // сброс таймера + запуск анимаций
    void stop();    // остановить анимации
    void pushLevel(qreal lvl);   // реальный уровень микрофона → волна
    int  seconds() const { return secs_; }   // длительность текущей записи

signals:
    void cancelClicked();
    void sendClicked();

private:
    PulseDot* dot_   = nullptr;
    LiveWave* wave_  = nullptr;
    QLabel*   time_  = nullptr;
    QTimer*   secTimer_ = nullptr;
    int       secs_ = 0;
};
