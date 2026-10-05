#pragma once
#include <QObject>
#include <QString>
#include <QByteArray>

class QMediaCaptureSession;
class QAudioInput;
class QMediaRecorder;
class QAudioSource;
class QIODevice;

// ─────────────────────────────────────────────────────────────────────────────
//  VoiceRecorder — запись голосового сообщения через Qt Multimedia в файл
//  MPEG-4/AAC (.m4a). По окончании отдаёт путь к файлу, MIME и PCM-дубликат
//  (48кГц/моно/16) для обрезки диапазона перед отправкой (VOX-01).
// ─────────────────────────────────────────────────────────────────────────────
class VoiceRecorder : public QObject {
    Q_OBJECT
public:
    explicit VoiceRecorder(QObject* parent = nullptr);

    bool start();     // начать запись (false — нет устройства ввода)
    void stop();      // завершить → recordingFinished(path, mime, pcm, durMs)
    void cancel();    // отменить без сигнала
    bool isRecording() const;

    // PCM [fromMs..toMs] → WAV-файл (48кГц/моно/16) для обрезанной отправки.
    static QByteArray pcmToWav(const QByteArray& pcm, int fromMs, int toMs);

signals:
    void recordingFinished(const QString& filePath, const QString& mimeType,
                           const QByteArray& pcmDuplicate, int pcmDurationMs);
    void error(const QString& message);
    // Реальный уровень микрофона (RMS чанка, dB-нормировка 0..1) —
    // для живой волны RecordingBar, чтобы она дышала от голоса.
    void inputLevel(qreal level);

private:
    void startPcmDuplication();   // VOX-01: параллельный QAudioSource
    void stopPcmDuplication();

    QMediaCaptureSession* session_;
    QAudioInput*          input_;
    QMediaRecorder*       recorder_;
    QAudioSource*         pcm_ = nullptr;   // дубликат для обрезки
    QIODevice*            pcmIo_ = nullptr;
    QByteArray            pcmDup_;
    int                   pcmMs_ = 0;
    bool                  canceled_ = false;
};
