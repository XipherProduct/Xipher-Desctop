#pragma once
#include <QObject>
#include <QByteArray>
#include <QTimer>

class QAudioSink;
class QIODevice;
class QAudioSource;

// ─────────────────────────────────────────────────────────────────────────────
//  CallSounds — звуки звонка, синтезированные в рантайме (как WebAudio в вебе,
//  js/calls.js playCallRingtone): файлов нет ни в одном клиенте намеренно.
//
//  Рингтон — мягкий двухнотный chime в стиле Telegram/Discord:
//    «динь» A5 (880 Гц, 0.5 c) и тише «дон» E5 (659.25 Гц, 0.62 c, +0.42 c),
//    каждая нота — синус + суб-октава (freq/2) с ADSR-огибкой
//    (атака 0.04 c, экспоненциальный спуск), повтор цикла каждые 2.4 c.
// ─────────────────────────────────────────────────────────────────────────────
class CallSounds : public QObject {
    Q_OBJECT
public:
    explicit CallSounds(QObject* parent = nullptr);
    ~CallSounds() override;

    // Цикл рингтона входящего звонка (стартует немедленно).
    void startRingtone();
    void stopRingtone();

    // Одноразовый короткий «динь» — соединение установлено.
    void playConnectChime();

    static CallSounds& instance();

private:
    void playPcm(const QByteArray& pcm);
    QByteArray synthesizeRing() const;    // один цикл «динь-дон»
    QByteArray synthesizeConnect() const; // короткое подтверждение

    QAudioSink* sink_ = nullptr;
    QIODevice*  io_   = nullptr;
    QTimer      loop_;
    bool        playing_ = false;
};
