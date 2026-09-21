#pragma once
#include <QObject>
#include <QTcpServer>
#include <QHash>
#include <functional>

class QTcpSocket;
class QNetworkReply;
class QNetworkAccessManager;

// ─────────────────────────────────────────────────────────────────────────────
//  MediaServer — локальный HTTP-прокси для стриминга медиа из API.
//
//  Зачем: плеер QMediaPlayer (GStreamer) умеет играть «пока качается» только по
//  http(s)-URL с Range-запросами, но не умеет подкладывать Authorization-заголовок,
//  а /files/* на сервере требует токен. Прокси слушает 127.0.0.1:<случайный порт>,
//  принимает GET /media?p=/files/… (с Range), пересылает запрос на сервер с
//  токеном и стримит ответ (206 Partial Content поддержан) обратно в сокет.
//  В итоге видео в чате открывается и проигрывается сразу, не дожидаясь
//  полной загрузки — как <video src> в веб-клиенте.
// ─────────────────────────────────────────────────────────────────────────────
class MediaServer : public QObject {
    Q_OBJECT
public:
    static MediaServer& instance();

    // bearer-токен берётся свежим на каждый запрос (сессия может обновиться).
    void setAuthProvider(std::function<QByteArray()> provider);
    void setRemoteBase(const QString& baseUrl);   // https://messenger.xipher.pro

    QString localBaseUrl() const;                 // http://127.0.0.1:<port>
    QString mediaUrl(const QString& serverPath) const;  // готовый URL для плеера

private:
    explicit MediaServer(QObject* parent = nullptr);
    void onNewConnection();
    void handleClient(QTcpSocket* client);
    void startProxy(QTcpSocket* client, const QString& path, const QString& range);

    QTcpServer*   server_ = nullptr;
    QNetworkAccessManager* nam_ = nullptr;
    QString remoteBase_;
    std::function<QByteArray()> authProvider_;

    struct Proxy {
        QNetworkReply* reply = nullptr;
        qint64 written = 0;
        bool headersSent = false;
    };
    QHash<QTcpSocket*, Proxy> proxies_;
};
