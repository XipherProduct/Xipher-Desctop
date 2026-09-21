#include "ui/AnimatedEmojiLabel.h"

#include <QMovie>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QShowEvent>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QDir>
#include <QFile>

namespace {

QString decodeCp(const QString& cp) {
    QString out;
    QString norm = cp;
    norm.replace('-', '_');
    for (const QString& part : norm.split('_')) {
        bool ok = false;
        char32_t v = part.toUInt(&ok, 16);
        if (ok) out += QString::fromUcs4(&v, 1);
    }
    return out;
}

// Загрузчик GIF Noto Animated Emoji с дисковым кэшем.
struct EmojiLoader {
    QNetworkAccessManager nam;
    QString dir;
    EmojiLoader() {
        dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (dir.isEmpty()) dir = QDir::tempPath();
        dir += QStringLiteral("/xipher_emoji");
        QDir().mkpath(dir);
    }
    void fetch(const QString& cp, std::function<void(QString)> cb) {
        const QString local = dir + QStringLiteral("/") + cp + QStringLiteral(".gif");
        if (QFile::exists(local)) { cb(local); return; }
        const QString url = QStringLiteral(
            "https://fonts.gstatic.com/s/e/notoemoji/latest/%1/512.gif").arg(cp);
        QNetworkReply* r = nam.get(QNetworkRequest(QUrl(url)));
        QObject::connect(r, &QNetworkReply::finished, r, [r, local, cb]() {
            r->deleteLater();
            if (r->error() != QNetworkReply::NoError) return;
            const QByteArray data = r->readAll();
            if (data.isEmpty()) return;
            QFile f(local);
            if (f.open(QIODevice::WriteOnly)) { f.write(data); f.close(); cb(local); }
        });
    }
};
EmojiLoader& loader() { static EmojiLoader l; return l; }

} // namespace

QString AnimatedEmojiLabel::emojiToCodepoints(const QString& emoji) {
    QString out;
    const QList<uint> ucs = emoji.toUcs4();
    for (uint c : ucs) {
        if (!out.isEmpty()) out += '_';
        out += QString::number(c, 16);
    }
    return out;
}

AnimatedEmojiLabel::AnimatedEmojiLabel(const QString& codepoints, int size, bool autoplay, QWidget* parent)
    : QLabel(parent), cp_(codepoints), size_(size), autoplay_(autoplay) {
    ch_ = decodeCp(cp_);
    setFixedSize(size_ + (autoplay_ ? 0 : 8), size_ + (autoplay_ ? 0 : 8));
    setAlignment(Qt::AlignCenter);
    if (!autoplay_) {
        setCursor(Qt::PointingHandCursor);
        setStyleSheet(QStringLiteral(
            "QLabel{border-radius:8px;} QLabel:hover{background:rgba(255,255,255,0.10);}"));
    }
    QFont f(QStringLiteral("Segoe UI Emoji"));
    f.setPixelSize(int(size_ * (autoplay_ ? 0.95 : 0.82)));
    setFont(f);
    setText(ch_);
}

void AnimatedEmojiLabel::play() {
    if (movie_) { movie_->start(); return; }
    // Ответ с CDN приходит асинхронно, а лейбл к этому моменту может быть уже
    // уничтожен (перерисовка чата, закрытие пикера). QPointer в колбэке —
    // единственная защита от висячего this: сырой указатель здесь падает
    // в QLabel::setMovie → QObject::connect.
    QPointer<AnimatedEmojiLabel> guard(this);
    loader().fetch(cp_, [guard](const QString& path) {
        if (AnimatedEmojiLabel* self = guard.data())
            self->startMovie(path);
    });
}

void AnimatedEmojiLabel::startMovie(const QString& path) {
    if (movie_) return;
    if (!autoplay_ && !underMouse()) return;   // курсор уже ушёл
    movie_ = new QMovie(path, QByteArray(), this);
    if (!movie_->isValid()) { movie_->deleteLater(); movie_ = nullptr; return; }
    movie_->setScaledSize(QSize(size_, size_));
    setMovie(movie_);
    movie_->start();
}

void AnimatedEmojiLabel::stop() {
    if (movie_) { movie_->stop(); movie_->deleteLater(); movie_ = nullptr; }
    setText(ch_);
}

void AnimatedEmojiLabel::showEvent(QShowEvent* e) {
    QLabel::showEvent(e);
    if (autoplay_) play();
}

void AnimatedEmojiLabel::enterEvent(QEnterEvent*) {
    if (!autoplay_) play();
}

void AnimatedEmojiLabel::leaveEvent(QEvent*) {
    if (!autoplay_) stop();
}

void AnimatedEmojiLabel::mouseReleaseEvent(QMouseEvent*) {
    if (onClick_) onClick_(ch_);
}
