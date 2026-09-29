#include "ui/CallSounds.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QMediaDevices>
#include <QIODevice>
#include <QtMath>

namespace {
// 48 кГц, моно, 16 бит — как аудиотракт CallEngine.
constexpr int kSampleRate = 48000;

// Один тон: синус f + суб-октава f/2, атака 0.04 c линейно до пиковой
// амплитуды, дальше экспоненциальный спуск до тишины к концу ноты
// (playNote из js/calls.js: пики 0.18/0.05, экспонента до 0.0001).
void addNote(QByteArray& pcm, double freq, double startSec, double durSec) {
    const int start = int(startSec * kSampleRate);
    const int len = int((0.04 + durSec) * kSampleRate) + 1;
    const double peak = 0.18 + 0.05;
    const double floor = 0.0001;
    for (int i = 0; i < len; ++i) {
        const double t = double(i) / kSampleRate;
        double env;
        if (t < 0.04) env = peak * (t / 0.04);
        else env = peak * std::pow(floor / peak, (t - 0.04) / durSec);
        const double main = std::sin(2 * M_PI * freq * t);
        const double sub = std::sin(M_PI * freq * t) * (0.05 / 0.18);
        const qint16 s = qint16(qBound(-32768.0, 32767.0 * env * (main + sub), 32767.0));
        const int idx = (start + i) * 2;
        if (idx + 1 >= pcm.size()) pcm.resize(idx + 2);
        // Смешиваем с уже записанным («динь» и «дон» перекрываются на 80 мс).
        const qint16 prev = qint16(quint8(pcm[idx]) | (quint8(pcm[idx + 1]) << 8));
        const qint16 mix = qint16(qBound(-32768, int(prev) + int(s), 32767));
        pcm[idx] = char(quint16(mix) & 0xFF);
        pcm[idx + 1] = char((quint16(mix) >> 8) & 0xFF);
    }
}
} // namespace

CallSounds::CallSounds(QObject* parent) : QObject(parent) {
    loop_.setInterval(2400);   // период цикла рингтона (2.4 c в вебе)
    connect(&loop_, &QTimer::timeout, this, [this]() { playPcm(synthesizeRing()); });
}

CallSounds::~CallSounds() {
    stopRingtone();
    if (sink_) sink_->stop();
}

CallSounds& CallSounds::instance() {
    // Намеренно «вечный» синглтон: деструктор статического объекта сработал бы
    // ПОСЛЕ смерти QApplication, и Qt Multimedia падала на выходе (проверено
    // design-verify: SIGSEGV в ~QAudioSink на exit()).
    static CallSounds* s = new CallSounds;
    return *s;
}

QByteArray CallSounds::synthesizeRing() const {
    // ring() из веба: playNote(880, now, 0.5) + playNote(659.25, now+0.42, 0.62)
    QByteArray pcm;
    addNote(pcm, 880.0, 0.0, 0.5);
    addNote(pcm, 659.25, 0.42, 0.62);
    return pcm;
}

QByteArray CallSounds::synthesizeConnect() const {
    QByteArray pcm;
    addNote(pcm, 880.0, 0.0, 0.28);
    return pcm;
}

void CallSounds::startRingtone() {
    stopRingtone();
    playPcm(synthesizeRing());
    loop_.start();
}

void CallSounds::stopRingtone() {
    loop_.stop();
    playing_ = false;
}

void CallSounds::playConnectChime() {
    if (loop_.isActive()) return;   // рингтон не перебиваем
    playPcm(synthesizeConnect());
}

void CallSounds::playPcm(const QByteArray& pcm) {
    const QAudioDevice dev = QMediaDevices::defaultAudioOutput();
    if (dev.isNull()) return;   // нет устройства вывода (offscreen/CI) — тихо
    QAudioFormat f;
    f.setSampleRate(kSampleRate);
    f.setChannelCount(1);
    f.setSampleFormat(QAudioFormat::Int16);
    if (!dev.isFormatSupported(f)) {
        f = dev.preferredFormat();
        if (f.sampleRate() <= 0 || f.channelCount() <= 0) return;
    }
    if (!sink_) sink_ = new QAudioSink(dev, f, this);
    if (sink_->state() == QAudio::ActiveState) sink_->stop();   // перебиваем
    io_ = sink_->start();   // push-режим
    if (!io_) return;
    playing_ = true;
    if (f.sampleRate() == kSampleRate && f.channelCount() == 1
        && f.sampleFormat() == QAudioFormat::Int16) {
        io_->write(pcm);
        return;
    }
    // Устройство захотело другой формат — простая передискретизация.
    const double rate = double(f.sampleRate()) / kSampleRate;
    const int channels = f.channelCount();
    if (f.sampleFormat() == QAudioFormat::Int16) {
        QByteArray out;
        out.reserve(int(pcm.size() * rate) * channels);
        const int samples = pcm.size() / 2;
        for (int i = 0; i < int(samples / rate); ++i) {
            const qint16 s = qint16(quint8(pcm[int(i / rate) * 2])
                                    | (quint8(pcm[int(i / rate) * 2 + 1]) << 8));
            for (int c = 0; c < channels; ++c) {
                out.append(char(quint16(s) & 0xFF));
                out.append(char((quint16(s) >> 8) & 0xFF));
            }
        }
        io_->write(out);
    } else {
        io_->write(pcm);   // лучший вариант из доступных
    }
}
