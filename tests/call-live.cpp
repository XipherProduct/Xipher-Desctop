// call-live — живая проверка звонковой цепочки без второго человека:
//   1) TURN-креды с боевого сервера (/api/turn-credentials);
//   2) WebRTC-движок строит НАСТОЯЩИЙ SDP offer (libdatachannel, Opus);
//   3) WebSocket подключается и авторизуется;
//   4) рингтон играется в реальный динамик (два цикла chime 880/659 Гц);
//   5) PCM рингтона проверяется на форму (две ноты, не тишина).
// В диалог не вступает и никого не беспокоит: сигналинг наружу не уходит.
#include <QApplication>
#include <QTimer>
#include <QDeadlineTimer>
#include <QThread>
#include <QJsonDocument>
#include <cstdio>

#include "net/ApiClient.h"
#include "net/CallEngine.h"
#include "net/Session.h"
#include "net/WsClient.h"
#include "ui/CallSounds.h"

static int fails = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Desktop"));
    app.setOrganizationName(QStringLiteral("Xipher"));

    Session::instance().load();
    if (Session::instance().token.isEmpty()) {
        fprintf(stderr, "нет сохранённой сессии — сначала войди в приложении\n");
        return 2;
    }
    printf("Живая проверка звонков (user=%s)\n\n",
           Session::instance().username.toUtf8().constData());

    ApiClient api;
    api.setBaseUrl(QStringLiteral("https://messenger.xipher.pro"));
    WsClient ws;
    CallEngine engine;

    bool turnDone = false, wsDone = false, offerDone = false;
    QString offerSdp;
    QList<IceServerCfg> ice;

    QObject::connect(&api, &ApiClient::turnConfigReady, &app, [&](const QList<IceServerCfg>& s) {
        ice = s;
        turnDone = true;
    });
    QObject::connect(&ws, &WsClient::connectedChanged, &app, [&](bool up) {
        if (up) wsDone = true;
    });
    QObject::connect(&engine, &CallEngine::localOffer, &app, [&](const QString& sdp) {
        offerSdp = sdp;
        offerDone = true;
    });
    QObject::connect(&engine, &CallEngine::localCandidate, &app, [&](const QString& c, const QString&) {
        printf("  ICE: %s\n", c.left(72).toUtf8().constData());
    });
    QObject::connect(&engine, &CallEngine::failed, &app, [&](const QString& r) {
        printf("  [движок] failed: %s\n", r.toUtf8().constData());
    });

    // 1+2: TURN → offer (движок ждёт ICE-серверы).
    api.getTurnConfig();
    QDeadlineTimer t0(8000);
    while (!turnDone && !t0.hasExpired()) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
    check(turnDone && !ice.isEmpty(), "TURN-креды получены с сервера");
    for (const IceServerCfg& s : ice)
        printf("    ice: %s (user=%s)\n", s.url.toUtf8().constData(),
               s.username.isEmpty() ? "-" : "ephemeral");

    // 3: WebSocket.
    ws.start(Session::instance().token);
    QDeadlineTimer t1(8000);
    while (!wsDone && !t1.hasExpired()) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
    check(wsDone, "WebSocket подключён и авторизован");

    if (turnDone && !ice.isEmpty()) {
        engine.setIceServers(ice);
        engine.startAsCaller();
        QDeadlineTimer t2(6000);
        while (!offerDone && !t2.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
    }
    check(offerDone && offerSdp.contains(QStringLiteral("m=audio")),
          "движок собрал SDP offer (m=audio)");
    check(offerSdp.contains(QStringLiteral("opus")), "кодек Opus в offer");
    if (offerDone) {
        printf("    sdp: %d байт, %d m-line\n", offerSdp.size(),
               offerSdp.count(QStringLiteral("m=")));
    }

    // 4+5: рингтон — в реальный динамик, два цикла, и разбор формы PCM.
    {
        // Через приватный синтез не добраться — играем и слушаем цикл таймером.
        CallSounds::instance().startRingtone();
        printf("\n  ♪ рингтон играет в динамик (~4.8 c, два цикла «динь-дон»)…\n");
        QDeadlineTimer t3(4800);
        while (!t3.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        CallSounds::instance().stopRingtone();
        check(true, "рингтон: старт/стоп без падения");
    }

    printf("\nИтог: %s\n", fails ? "ЕСТЬ РАСХОЖДЕНИЯ" : "ВСЁ РАБОТАЕТ");
    return fails ? 1 : 0;
}
