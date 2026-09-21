// mediaserver-test.cpp — проверка локального Range-прокси (MediaServer):
//   • полный GET отдаёт тело целиком и 200;
//   • «Range: bytes=a-b» отдаёт ровно запрошенный кусок (206);
//   • суффикс «Range: bytes=-N» — последние N байт;
//   • незнакомый путь → 404.
// В качестве «сервера» используется локальный http-сервер теста (QTcpServer),
// чтобы тест не зависел от сети.
//
// Сборка (из корня):
//   g++ -fPIC tests/mediaserver-test.cpp src/net/MediaServer.cpp \
//       src/net/Session.cpp -I src -o /tmp/ms-test \
//       $(pkg-config --cflags --libs Qt6Core Qt6Network)
// Запуск: REMOTE_BASE=http://127.0.0.1:<порт> /tmp/ms-test

#include "net/MediaServer.h"
#include "net/Session.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const QString& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toUtf8().constData());
    if (!ok) ++failures;
}

// Примитивный «прод-сервер»: отдаёт /files/sample.bin (64 KiB паттерна),
// поддерживает Range.
class FakeOrigin : public QTcpServer {
public:
    QByteArray payload;
    FakeOrigin() {
        payload.resize(64 * 1024);
        for (int i = 0; i < payload.size(); ++i) payload[i] = char(i * 7 + 3);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    reqs[s] += s->readAll();
                    const int headEnd = reqs[s].indexOf("\r\n\r\n");
                    if (headEnd < 0) return;
                    const QByteArray head = reqs[s].left(headEnd);
                    const QByteArray line = head.left(head.indexOf("\r\n"));
                    const QList<QByteArray> parts = line.split(' ');
                    const QByteArray path = parts.value(1);
                    qint64 start = 0, end = payload.size() - 1;
                    bool isRange = false;
                    for (const QByteArray& l : head.split('\n')) {
                        if (l.startsWith(QByteArrayLiteral("Range: bytes="))) {
                            isRange = true;
                            const QByteArray spec = l.mid(13).trimmed();
                            const int dash = spec.indexOf('-');
                            const QByteArray a = spec.left(dash), b = spec.mid(dash + 1);
                            if (!a.isEmpty()) {
                                start = a.toLongLong();
                                if (!b.isEmpty()) end = b.toLongLong();
                            } else {
                                start = payload.size() - b.toLongLong();
                                end = payload.size() - 1;
                            }
                        }
                    }
                    QByteArray resp;
                    if (path == QByteArrayLiteral("/files/sample.bin")) {
                        const QByteArray body = payload.mid(int(start), int(end - start + 1));
                        resp += isRange ? QByteArrayLiteral("HTTP/1.1 206 Partial Content\r\n")
                                        : QByteArrayLiteral("HTTP/1.1 200 OK\r\n");
                        if (isRange)
                            resp += "Content-Range: bytes " + QByteArray::number(start)
                                  + "-" + QByteArray::number(end) + "/"
                                  + QByteArray::number(payload.size()) + "\r\n";
                        resp += "Content-Type: application/octet-stream\r\n";
                        resp += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
                        resp += "Connection: close\r\n\r\n";
                        resp += body;
                    } else {
                        resp += QByteArrayLiteral("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    }
                    s->write(resp);
                    s->disconnectFromHost();
                    reqs.remove(s);
                });
                connect(s, &QTcpSocket::disconnected, s, &QTcpSocket::deleteLater);
            }
        });
    }
    QHash<QTcpSocket*, QByteArray> reqs;
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    FakeOrigin origin;
    origin.listen(QHostAddress::LocalHost);
    const QString remote = QStringLiteral("http://127.0.0.1:%1").arg(origin.serverPort());

    qputenv("REMOTE_BASE", remote.toUtf8());

    Session::instance().token = QStringLiteral("test-token");
    MediaServer::instance().setRemoteBase(remote);
    MediaServer::instance().setAuthProvider([] {
        return Session::instance().token.toUtf8();
    });

    QNetworkAccessManager nam;
    const QString url = MediaServer::instance().mediaUrl(QStringLiteral("/files/sample.bin"));
    printf("MediaServer: %s\n", MediaServer::instance().localBaseUrl().toUtf8().constData());

    auto get = [&](const QByteArray& range) {
        QNetworkRequest req{QUrl(url)};
        if (!range.isEmpty()) req.setRawHeader(QByteArrayLiteral("Range"), range);
        QNetworkReply* reply = nam.get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(4000, &loop, &QEventLoop::quit);
        loop.exec();
        return reply;
    };

    printf("1) Полный GET\n");
    {
        QNetworkReply* r = get(nullptr);
        check(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200,
              QStringLiteral("статус 200"));
        check(r->readAll() == origin.payload, QStringLiteral("тело совпадает целиком (64 КиБ)"));
        delete r;
    }

    printf("2) Range: bytes=100-109\n");
    {
        QNetworkReply* r = get(QByteArrayLiteral("bytes=100-109"));
        check(r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 206,
              QStringLiteral("статус 206"));
        const QByteArray body = r->readAll();
        check(body.size() == 10 && body == origin.payload.mid(100, 10),
              QStringLiteral("отдан ровно запрошенный кусок"));
        delete r;
    }

    printf("3) Range: bytes=-64 (суффикс)\n");
    {
        QNetworkReply* r = get(QByteArrayLiteral("bytes=-64"));
        const QByteArray body = r->readAll();
        check(body.size() == 64 && body == origin.payload.right(64),
              QStringLiteral("суффикс-диапазон отдаёт хвост"));
        delete r;
    }

    printf("4) Незнакомый путь\n");
    {
        QNetworkReply* r = get(nullptr);
        QNetworkRequest badReq{QUrl(MediaServer::instance().mediaUrl(QStringLiteral("/files/none.bin")))};
        delete r;
        QNetworkReply* r2 = nam.get(badReq);
        QEventLoop loop;
        QObject::connect(r2, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(4000, &loop, &QEventLoop::quit);
        loop.exec();
        check(r2->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 404,
              QStringLiteral("нет файла — 404"));
        delete r2;
    }

    printf("Итог: %s (%d ошибок)\n", failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ОК", failures);
    return failures ? 1 : 0;
}
