#include "util/MprisAdapter.h"

#ifdef Q_OS_UNIX
#include <QDBusConnection>
#include <QDBusMessage>
#include <QVariantMap>

MprisAdapter::MprisAdapter(QObject* playerOwner, QObject* parent)
    : QObject(parent) {
    Q_UNUSED(playerOwner);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;   // offscreen/тесты — без шины
    auto* adaptor = new MprisPlayerAdaptor(this);
    Q_UNUSED(adaptor);
    if (!bus.registerService(QStringLiteral("org.mpris.MediaPlayer2.xipher"))) return;
    // Корневой объект MPRIS: требуется и /org/mpris/MediaPlayer2 — сам this
    // отдаёт адаптер Player; корневые свойства отдаёт отдельный простой объект.
    registered_ = bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                                     QDBusConnection::ExportAdaptors);
}

void MprisAdapter::setMedia(const QString& title, const QString& artist) {
    title_ = title;
    artist_ = artist;
}

void MprisAdapter::setPlaying(bool playing) {
    if (playing_ != playing) {
        playing_ = playing;
        // Уведомление MPRIS-клиентов о смене статуса.
        auto bus = QDBusConnection::sessionBus();
        if (bus.isConnected()) {
            QDBusMessage sig = QDBusMessage::createSignal(
                QStringLiteral("/org/mpris/MediaPlayer2"),
                QStringLiteral("org.mpris.MediaPlayer2.Player"),
                QStringLiteral("Seeked"));
            bus.send(sig);
        }
    }
}

// ── Адаптер ─────────────────────────────────────────────────────────────────

MprisPlayerAdaptor::MprisPlayerAdaptor(MprisAdapter* owner)
    : QDBusAbstractAdaptor(owner), owner_(owner) {}

QString MprisPlayerAdaptor::playbackStatus() const {
    return owner_->isPlaying() ? QStringLiteral("Playing")
                               : QStringLiteral("Paused");
}

QVariantMap MprisPlayerAdaptor::metadata() const {
    QVariantMap md;
    md.insert(QStringLiteral("mpris:trackid"),
              QVariant::fromValue(QString()));
    md.insert(QStringLiteral("mpris:title"), owner_->title());
    if (!owner_->artist().isEmpty())
        md.insert(QStringLiteral("mpris:artist"), owner_->artist());
    return md;
}

void MprisPlayerAdaptor::PlayPause() { emit owner_->playPauseRequested(); }
void MprisPlayerAdaptor::Play() { emit owner_->playPauseRequested(); }
void MprisPlayerAdaptor::Pause() { emit owner_->playPauseRequested(); }
void MprisPlayerAdaptor::Stop() { emit owner_->stopRequested(); }
void MprisPlayerAdaptor::Next() { emit owner_->nextRequested(); }
void MprisPlayerAdaptor::Previous() { emit owner_->previousRequested(); }

#else // !Q_OS_UNIX

MprisAdapter::MprisAdapter(QObject* playerOwner, QObject* parent)
    : QObject(parent) {
    Q_UNUSED(playerOwner);
}

void MprisAdapter::setMedia(const QString& title, const QString& artist) {
    title_ = title; artist_ = artist;
}

void MprisAdapter::setPlaying(bool playing) { playing_ = playing; }

#endif // Q_OS_UNIX
