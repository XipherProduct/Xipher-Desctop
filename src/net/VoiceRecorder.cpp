#include "net/VoiceRecorder.h"

#include <QMediaCaptureSession>
#include <QAudioInput>
#include <QMediaRecorder>
#include <QMediaFormat>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioSource>
#include <QUrl>
#include <QDir>
#include <QDateTime>
#include <QTimer>

namespace {
// PCM-дубликат для обрезки (VOX-01): 48кГц/моно/int16 = 96 байт/мс.
constexpr int kPcmBytesPerMs = 96;
}

VoiceRecorder::VoiceRecorder(QObject* parent)
    : QObject(parent),
      session_(new QMediaCaptureSession(this)),
      input_(new QAudioInput(this)),
      recorder_(new QMediaRecorder(this)) {
    session_->setAudioInput(input_);
    session_->setRecorder(recorder_);

    QMediaFormat format;
    format.setFileFormat(QMediaFormat::MPEG4);
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    recorder_->setMediaFormat(format);
    recorder_->setQuality(QMediaRecorder::HighQuality);

    connect(recorder_, &QMediaRecorder::recorderStateChanged, this,
            [this](QMediaRecorder::RecorderState state) {
        if (state == QMediaRecorder::StoppedState) {
            stopPcmDuplication();
            if (canceled_) return;
            const QString path = recorder_->actualLocation().toLocalFile();
            if (path.isEmpty()) {
                emit error(QStringLiteral("Запись не сохранена"));
                return;
            }
            emit recordingFinished(path, QStringLiteral("audio/mp4"), pcmDup_, pcmMs_);
        }
    });
    connect(recorder_, &QMediaRecorder::errorOccurred, this,
            [this](QMediaRecorder::Error, const QString& msg) {
        if (!canceled_) emit error(msg.isEmpty() ? QStringLiteral("Ошибка записи") : msg);
    });
}

bool VoiceRecorder::start() {
    if (QMediaDevices::defaultAudioInput().isNull()) {
        emit error(QStringLiteral("Микрофон не найден"));
        return false;
    }
    canceled_ = false;
    pcmDup_.clear();
    pcmMs_ = 0;
    const QString path = QDir::temp().filePath(
        QStringLiteral("xipher_voice_%1.m4a").arg(QDateTime::currentMSecsSinceEpoch()));
    recorder_->setOutputLocation(QUrl::fromLocalFile(path));
    recorder_->record();
    startPcmDuplication();   // VOX-01: параллельный PCM для обрезки диапазона
    return true;
}

// ── PCM-дубликат (VOX-01) ────────────────────────────────────────────────────

void VoiceRecorder::startPcmDuplication() {
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!QMediaDevices::defaultAudioInput().isFormatSupported(fmt)) return;
    pcm_ = new QAudioSource(QMediaDevices::defaultAudioInput(), fmt, this);
    pcmIo_ = pcm_->start();
    if (!pcmIo_) { pcm_->deleteLater(); pcm_ = nullptr; return; }
    connect(pcmIo_, &QIODevice::readyRead, this, [this]() {
        const QByteArray chunk = pcmIo_->readAll();
        pcmDup_.append(chunk);
        pcmMs_ = pcmDup_.size() / kPcmBytesPerMs;
    });
}

void VoiceRecorder::stopPcmDuplication() {
    if (pcm_) {
        pcm_->stop();
        pcm_->deleteLater();
        pcm_ = nullptr;
        pcmIo_ = nullptr;
    }
}

// Срез PCM [fromMs..toMs] → WAV (48кГц/моно/16) для отправки (VOX-01).
QByteArray VoiceRecorder::pcmToWav(const QByteArray& pcm, int fromMs, int toMs) {
    const int start = qMax(0, fromMs) * kPcmBytesPerMs / 2 * 2;
    const int end = qMin<int>(pcm.size(), qMax(0, toMs) * kPcmBytesPerMs);
    if (end <= start) return QByteArray();
    const QByteArray data = pcm.mid(start, end - start);
    const qint32 dataSize = data.size();
    const qint32 byteRate = 48000 * 2;
    QByteArray wav;
    wav.reserve(44 + dataSize);
    auto le = [&wav](const void* p, int n) { wav.append(reinterpret_cast<const char*>(p), n); };
    wav.append("RIFF", 4);
    const qint32 riffSize = 36 + dataSize;
    le(&riffSize, 4);
    wav.append("WAVEfmt ", 8);
    const qint32 fmtSize = 16;
    le(&fmtSize, 4);
    const qint16 audioFormat = 1, channels = 1, bits = 16;
    const qint32 sampleRate = 48000;
    le(&audioFormat, 2); le(&channels, 2); le(&sampleRate, 4);
    le(&byteRate, 4);
    const qint16 blockAlign = 2;
    le(&blockAlign, 2); le(&bits, 2);
    wav.append("data", 4);
    le(&dataSize, 4);
    wav.append(data);
    return wav;
}

void VoiceRecorder::stop() {
    canceled_ = false;
    if (recorder_->recorderState() != QMediaRecorder::StoppedState)
        recorder_->stop();
    else
        stopPcmDuplication();
}

void VoiceRecorder::cancel() {
    canceled_ = true;
    if (recorder_->recorderState() != QMediaRecorder::StoppedState)
        recorder_->stop();
    else
        stopPcmDuplication();
}

bool VoiceRecorder::isRecording() const {
    return recorder_->recorderState() == QMediaRecorder::RecordingState;
}
