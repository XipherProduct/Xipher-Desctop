#include "net/MediaServer.h"
#include "net/ApiClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

MediaServer& MediaServer::instance() {
    static MediaServer s;
    return s;
}

MediaServer::MediaServer(QObject* parent) : QObject(parent) {
    server_ = new QTcpServer(this);
    connect(server_, &QTcpServer::newConnection, this, &MediaServer::onNewConnection);
    server_->listen(QHostAddress::LocalHost);   // случайный свободный порт
    remoteBase_ = QStringLiteral("https://messenger.xipher.pro");
    authProvider_ = [] { return QByteArray(); };
    nam_ = new QNetworkAccessManager(this);
}

void MediaServer::setAuthProvider(std::function<QByteArray()> provider) {
    authProvider_ = std::move(provider);
}

void MediaServer::setRemoteBase(const QString& baseUrl) {
    remoteBase_ = baseUrl;
}

QString MediaServer::localBaseUrl() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(server_->serverPort());
}

QString MediaServer::mediaUrl(const QString& serverPath) const {
    QUrl u(localBaseUrl() + QStringLiteral("/media"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("p"), serverPath);
    u.setQuery(q);
    return u.toString();
}

void MediaServer::onNewConnection() {
    while (QTcpSocket* client = server_->nextPendingConnection()) {
        connect(client, &QTcpSocket::disconnected, client, &QTcpSocket::deleteLater);
        connect(client, &QTcpSocket::readyRead, this, [this, client] { handleClient(client); });
    }
}

void MediaServer::handleClient(QTcpSocket* client) {
    if (proxies_.contains(client)) return;              // запрос уже проксируется
    // HTTP-запрос читаем целиком (заголовки заканчиваются пустой строкой).
    if (!client->canReadLine()) {
        // ждём накопления — упрощение: заголовки приходят одним сегментом
    }
    QByteArray req = client->readAll();
    const int headEnd = req.indexOf("\r\n\r\n");
    if (headEnd < 0) return;                            // ждём ещё данных

    const QByteArray requestLine = req.left(req.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2) { client->disconnectFromHost(); return; }

    QUrl url(QString::fromUtf8(parts[1]));
    QUrlQuery q(url);
    const QString path = q.queryItemValue(QStringLiteral("p"));

    QByteArray range;
    const QList<QByteArray> lines = req.left(headEnd).split('\n');
    for (const QByteArray& line : lines) {
        if (line.startsWith(QByteArrayLiteral("Range:")) || line.startsWith(QByteArrayLiteral("range:")))
            range = line.mid(6).trimmed();
    }

    if (path.isEmpty()) {
        client->write("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n");
        client->flush();
        return;
    }
    startProxy(client, path, range);
}

void MediaServer::startProxy(QTcpSocket* client, const QString& path, const QString& range) {
    // Предохранитель: плеер при проблемах открывает соединения снова и снова —
    // без лимита это каскад параллельных закачек (лаг/фриз всего приложения).
    if (proxies_.size() >= 8) {
        client->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        client->flush();
        client->disconnectFromHost();
        return;
    }
    QNetworkRequest req(QUrl(remoteBase_ + path));
    if (!authProvider_) authProvider_ = [] { return QByteArray(); };
    req.setRawHeader(QByteArrayLiteral("Authorization"),
                     QByteArrayLiteral("Bearer ") + authProvider_());
    if (!range.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Range"), range.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = nam_->get(req);
    Proxy& proxy = proxies_[client];
    proxy.reply = reply;
    proxy.written = 0;
    proxy.headersSent = false;

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, client]() {
        auto it = proxies_.find(client);
        if (it == proxies_.end() || it->headersSent) return;
        QNetworkReply* reply = it->reply;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) return;                        // метаданные ещё не пришли
        it->headersSent = true;
        QByteArray head = "HTTP/1.1 " + QByteArray::number(status) + " OK\r\n";
        const QString ctype = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        if (!ctype.isEmpty()) head += "Content-Type: " + ctype.toUtf8() + "\r\n";
        const QByteArray contentRange =
            reply->rawHeader(QByteArrayLiteral("Content-Range"));
        if (!contentRange.isEmpty())
            head += "Content-Range: " + contentRange + "\r\n";
        head += "Accept-Ranges: bytes\r\n";
        const qint64 size = reply->size() > 0
            ? reply->size()
            : reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (size > 0) head += "Content-Length: " + QByteArray::number(size) + "\r\n";
        head += "Connection: close\r\n\r\n";
        client->write(head);
        client->flush();
    });

    auto watchdog = new QTimer(reply);
    watchdog->setSingleShot(true);
    watchdog->setInterval(30000);
    connect(watchdog, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::readyRead, this, [this, client, watchdog]() {
        auto it = proxies_.find(client);
        if (it == proxies_.end()) return;
        watchdog->start();                       // данные идут — сторож ждёт дальше
        const QByteArray data = it->reply->readAll();
        it->written += data.size();
        client->write(data);
        client->flush();
    });
    watchdog->start();

    connect(reply, &QNetworkReply::finished, this, [this, client, reply]() {
        if (auto it = proxies_.find(client); it != proxies_.end() && !it->headersSent) {
            // Ошибка до заголовков (401/404/сеть) — честный ответ клиенту.
            int status = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 0) status = 502;
            client->write("HTTP/1.1 " + QByteArray::number(status) +
                          " Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            client->flush();
        }
        client->disconnectFromHost();
        reply->deleteLater();
        proxies_.remove(client);
    });

    connect(client, &QTcpSocket::disconnected, this, [this, client]() {
        if (auto it = proxies_.find(client); it != proxies_.end()) {
            it->reply->abort();
            it->reply->deleteLater();
            proxies_.remove(client);   // сокет удалится по deleteLater
        }
    });
}
