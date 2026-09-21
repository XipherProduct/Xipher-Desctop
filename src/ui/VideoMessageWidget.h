#pragma once
#include <QWidget>
#include <QPointer>

class QLabel;
class QProgressBar;
class QPushButton;
class QVBoxLayout;
class QMediaPlayer;
class QAudioOutput;
class QVideoWidget;
class ApiClient;

// ─────────────────────────────────────────────────────────────────────────────
//  VideoMessageWidget — видео в чате как в Telegram:
//   • плеер внутри баббла; нажал ▶ — видео сразу играет, даже пока файл ещё
//     не скачался: стриминг идёт через локальный MediaServer (Range + токен),
//     QMediaPlayer буферизует, серым по прогресс-бару видно буфер;
//   • параллельно файл докачивается в зашифрованный кэш (FileCache) — после
//     первого просмотра открывается мгновенно с диска;
//   • прогресс: максимум из буфера плеера и загрузки (получено/всего);
//   • кэш-хит — играет локально без сети.
// ─────────────────────────────────────────────────────────────────────────────
class VideoMessageWidget : public QWidget {
    Q_OBJECT
public:
    VideoMessageWidget(ApiClient* api, const QString& serverPath,
                       const QString& fileName, qint64 fileSize,
                       bool circularVideoNote, QWidget* parent);

    bool eventFilter(QObject* obj, QEvent* e) override;
    void stopPlayback();   // полная остановка (смена чата / удаление строки)

protected:
    void hideEvent(QHideEvent* e) override;

private slots:
    void togglePlay();
    void onMediaStatusChanged(int status);
    void onBufferProgress(int filled);
    void onFileProgress(const QString& path, qint64 received, qint64 total);
    void onFileFetched(const QString& path, const QByteArray& bytes);

private:
    void buildUi();
    void startPlayback();
    void startBackgroundDownload();
    void updateProgress();
    QString localPlaySource();      // путь в кэше/на диске либо proxy-URL

    ApiClient* api_;
    QString serverPath_;
    QString fileName_;
    qint64  fileSize_ = 0;
    bool    circular_ = false;

    QVideoWidget* video_ = nullptr;
    QPushButton*  playBtn_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel*       info_ = nullptr;

    QMediaPlayer* player_ = nullptr;
    QAudioOutput* audio_ = nullptr;
    bool    playing_ = false;
    bool    started_ = false;
    bool    bgDownloading_ = false;
    bool    cached_ = false;
    int     bufferPct_ = 0;
    qint64  dlReceived_ = 0;
    qint64  dlTotal_ = 0;
    QPointer<VideoMessageWidget> selfGuard_;
};
