#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QObject>
#include <QString>

class QDBusMessage;

// ─────────────────────────────────────────────────────────────────────────────
//  MprisAdapter — MPRIS2 D-Bus интерфейс (MDV-05): системные мультимедиа-клавиши
//  (Play/Pause/Next/Previous) и SMTC-совместимые клиенты управляют плеером.
//  Регистрация org.mpris.MediaPlayer2.xipher на session bus; если шины нет
//  (offscreen/тесты) — адаптер просто неактивен.
// ─────────────────────────────────────────────────────────────────────────────
class MprisAdapter : public QObject {
    Q_OBJECT
public:
    explicit MprisAdapter(QObject* playerOwner, QObject* parent = nullptr);
    bool isActive() const { return registered_; }
    // Состояние для адаптера (читает MprisPlayerAdaptor).
    bool isPlaying() const { return playing_; }
    QString title() const { return title_; }
    QString artist() const { return artist_; }

    // Состояние для интерфейса (дергает владелец — ChatPage).
    void setMedia(const QString& title, const QString& artist = QString());
    void setPlaying(bool playing);

signals:
    // Системная команда → владелец плеера.
    void playPauseRequested();
    void nextRequested();
    void previousRequested();
    void stopRequested();

private:
    bool registered_ = false;
    QString title_, artist_;
    bool playing_ = false;
};

// Сам D-Bus-адаптер (свойства/методы MPRIS2, минимум для SMTC/GNOME).
class MprisPlayerAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)
    Q_PROPERTY(bool CanPlay READ canPlay CONSTANT)
    Q_PROPERTY(bool CanPause READ canPause CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canGoNext CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious CONSTANT)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
public:
    explicit MprisPlayerAdaptor(MprisAdapter* owner);

    bool canControl() const { return true; }
    bool canPlay() const { return true; }
    bool canPause() const { return true; }
    bool canGoNext() const { return true; }
    bool canGoPrevious() const { return true; }
    QString playbackStatus() const;
    QString loopStatus() const { return QStringLiteral("None"); }
    void setLoopStatus(const QString&) {}
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const { return false; }
    void setShuffle(bool) {}
    QVariantMap metadata() const;

public slots:
    void Next();
    void Previous();
    void PlayPause();
    void Play();
    void Pause();
    void Stop();

private:
    MprisAdapter* owner_;
};
