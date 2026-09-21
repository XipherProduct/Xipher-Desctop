#include "ui/VideoMessageWidget.h"
#include "net/ApiClient.h"
#include "net/FileCache.h"
#include "net/MediaServer.h"

#include <QAudioOutput>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMediaPlayer>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QtMultimediaWidgets/QVideoWidget>

namespace {

// Кэш-путь: те же файлы, что кладёт ChatPage в FileCache (единый источник).
QString humanSize(qint64 b) {
    if (b <= 0) return QString();
    if (b < 1024) return QStringLiteral("%1 Б").arg(b);
    if (b < 1024 * 1024) return QStringLiteral("%1 КБ").arg(b / 1024);
    return QStringLiteral("%1 МБ").arg(QString::number(b / 1048576.0, 'f', 1));
}

} // namespace

VideoMessageWidget::VideoMessageWidget(ApiClient* api, const QString& serverPath,
                                       const QString& fileName, qint64 fileSize,
                                       bool circularVideoNote, QWidget* parent)
    : QWidget(parent), api_(api), serverPath_(serverPath), fileName_(fileName),
      fileSize_(fileSize), circular_(circularVideoNote) {
    selfGuard_ = this;
    QByteArray cached;
    cached_ = FileCache::instance().lookup(serverPath_, &cached);
    buildUi();
    connect(api_, &ApiClient::fileProgress, this, &VideoMessageWidget::onFileProgress);
    connect(api_, &ApiClient::fileFetched, this, &VideoMessageWidget::onFileFetched);
}

void VideoMessageWidget::buildUi() {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    video_ = new QVideoWidget(this);
    if (circular_) {
        video_->setFixedSize(220, 220);
    } else {
        video_->setMinimumSize(320, 180);
        video_->setMaximumSize(480, 360);
    }
    video_->setAttribute(Qt::WA_StyledBackground, true);
    video_->setStyleSheet(QStringLiteral("background:#000;border-radius:%1;")
                              .arg(circular_ ? QStringLiteral("110px")
                                             : QStringLiteral("8px")));
    root->addWidget(video_, 0, Qt::AlignHCenter);

    playBtn_ = new QPushButton(video_);
    playBtn_->setCursor(Qt::PointingHandCursor);
    playBtn_->setFixedSize(56, 56);
    playBtn_->setText(QStringLiteral("▶"));
    playBtn_->setStyleSheet(QStringLiteral(
        "QPushButton{background:rgba(0,0,0,0.55);border:2px solid rgba(255,255,255,0.75);"
        "border-radius:28px;color:#fff;font-size:20px;padding-left:4px;}"
        "QPushButton:hover{background:rgba(139,92,246,0.8);}"));
    playBtn_->installEventFilter(this);
    playBtn_->move(circular_ ? 82 : 132, circular_ ? 82 : 60);
    connect(playBtn_, &QPushButton::clicked, this, &VideoMessageWidget::togglePlay);

    if (!circular_) {
        info_ = new QLabel(this);
        const QString name = fileName_.isEmpty() ? QStringLiteral("Видео") : fileName_;
        info_->setText(QStringLiteral("%1  %2")
                           .arg(name.toHtmlEscaped(), humanSize(fileSize_)));
        info_->setStyleSheet(QStringLiteral("color:#ACA6BD;font-size:12px;"));
        root->addWidget(info_);

        progress_ = new QProgressBar(this);
        progress_->setFixedHeight(4);
        progress_->setTextVisible(false);
        progress_->setRange(0, 100);
        progress_->setValue(0);
        progress_->setStyleSheet(QStringLiteral(
            "QProgressBar{background:rgba(255,255,255,0.12);border:none;border-radius:2px;}"
            "QProgressBar::chunk{background:#8B5CF6;border-radius:2px;}"));
        root->addWidget(progress_);
    }
}

void VideoMessageWidget::hideEvent(QHideEvent* e) {
    // Виджет скрылся (сменили чат/строку удалили) — не продолжаем декодировать.
    if (player_) player_->pause();
    playing_ = false;
    playBtn_->setText(QStringLiteral("▶"));
    QWidget::hideEvent(e);
}

void VideoMessageWidget::stopPlayback() {
    if (player_) {
        player_->stop();
        player_->setSource(QUrl());   // рвём активный стрим по прокси
    }
    started_ = false;
    playing_ = false;
    playBtn_->setText(QStringLiteral("▶"));
    if (bgDownloading_) {
        bgDownloading_ = false;
        api_->cancelFetch(serverPath_);   // фон-докачку тоже гасим
    }
}

bool VideoMessageWidget::eventFilter(QObject* obj, QEvent* e) {
    if (obj == video_ && e->type() == QEvent::MouseButtonRelease) {
        togglePlay();
        return true;
    }
    return QWidget::eventFilter(obj, e);
}

QString VideoMessageWidget::localPlaySource() {
    if (cached_) {
        QByteArray bytes;
        if (FileCache::instance().lookup(serverPath_, &bytes) && !bytes.isEmpty()) {
            const QString tmp = QDir::temp().filePath(
                QStringLiteral("xipher_video_%1").arg(qHash(serverPath_)) +
                QFileInfo(serverPath_).suffix().prepend('.'));
            QFile f(tmp);
            if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); }
            return QUrl::fromLocalFile(tmp).toString();
        }
        cached_ = false;
    }
    return MediaServer::instance().mediaUrl(serverPath_);
}

void VideoMessageWidget::togglePlay() {
    if (!player_) {
        player_ = new QMediaPlayer(this);
        audio_ = new QAudioOutput(this);
        player_->setAudioOutput(audio_);
        player_->setVideoOutput(video_);
        connect(player_, &QMediaPlayer::bufferProgressChanged, this,
                &VideoMessageWidget::onBufferProgress);
        connect(player_, &QMediaPlayer::mediaStatusChanged, this,
                &VideoMessageWidget::onMediaStatusChanged);
        connect(player_, &QMediaPlayer::playbackStateChanged, this, [this](int st) {
            playing_ = st == QMediaPlayer::PlayingState;
            playBtn_->setText(playing_ ? QStringLiteral("❚❚") : QStringLiteral("▶"));
        });
    }
    if (playing_) { player_->pause(); return; }
    if (!started_) startPlayback();
    else player_->play();
}

void VideoMessageWidget::startPlayback() {
    started_ = true;
    player_->setSource(QUrl(localPlaySource()));
    player_->play();
    audio_->setVolume(1.0);
    // Фоновую докачку в кэш стартуем только когда стриминг неактивен:
    // прокси-стрим + параллельная закачка того же файла = двойная нагрузка.
    if (!cached_ && !bgDownloading_) startBackgroundDownload();
}

void VideoMessageWidget::startBackgroundDownload() {
    bgDownloading_ = true;
    if (progress_) progress_->show();
    api_->fetchFile(serverPath_);   // фоновая докачка в кэш
}

void VideoMessageWidget::onBufferProgress(int filled) {
    bufferPct_ = int(filled * 100);
    updateProgress();
}

void VideoMessageWidget::onFileProgress(const QString& path, qint64 received, qint64 total) {
    if (path != serverPath_) return;
    dlReceived_ = received;
    dlTotal_ = total;
    updateProgress();
}

void VideoMessageWidget::onFileFetched(const QString& path, const QByteArray& bytes) {
    if (path != serverPath_ || bytes.isEmpty()) return;
    FileCache::instance().store(serverPath_, bytes);
    cached_ = true;
    dlReceived_ = dlTotal_ = 0;
    bgDownloading_ = false;
    updateProgress();
    if (progress_) progress_->hide();
    if (player_ && started_ && !playing_ && player_->mediaStatus() == QMediaPlayer::EndOfMedia) {
        // Перезапуск из полного локального файла — без сети.
        player_->setSource(QUrl(localPlaySource()));
    }
}

void VideoMessageWidget::updateProgress() {
    if (!progress_) return;
    int pct = bufferPct_;
    if (dlTotal_ > 0) pct = qMax(pct, int(dlReceived_ * 100 / dlTotal_));
    progress_->setValue(pct);
}

void VideoMessageWidget::onMediaStatusChanged(int status) {
    // Конец локального файла при стриминге: если полная версия ещё не в кэше —
    // перемотать и доиграть остаток (плеер сам дотянет по Range).
    Q_UNUSED(status);
}
