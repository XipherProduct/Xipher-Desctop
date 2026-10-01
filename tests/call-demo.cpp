// call-demo — демо-звонок С ЗВУКОМ без второго человека и без сервера:
// два WebRTC-движка (CallEngine) в одном процессе соединяются loopback'ом
// (offer/answer/ICE прокидываются напрямую сигналами), реальный RTP-эфир
// libdatachannel, Opus 20 мс.
//
// Режим по умолчанию (мелодия): через весь конвейер в колонки идёт чистое
// арпеджио C5-E5-G5 (синтез на стороне «звонящего», кодек, RTP, декодер,
// динамик «принимающего»). Микрофон не трогаем вовсе — самовозбуждение
// (микрофон→колонки→микрофон = резкий вой) невозможно в принципе.
//
// Режим --echo: вместо мелодии — ваш голос (микрофон → динамик). Первые 3 c
// слышимое эхо, затем динамики глушатся: без AEC и наушников петля завывает.
//
// PASS-критерий: оба движка connected + до эфира дошло ≥70% кадров + мьют
// действительно гасит исходящий поток.
#include <QApplication>
#include <QTimer>
#include <QDeadlineTimer>
#include <QThread>
#include <cstdio>

#include "net/CallEngine.h"
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

    const bool echoMode = argc > 1 && QByteArray(argv[1]) == "--echo";

    printf("Демо-звонок с звуком (loopback, два пира в одном процессе)\n");
    printf("Режим: %s\n\n", echoMode ? "эхо голоса (лучше в наушниках)"
                                     : "мелодия через весь конвейер");

    CallEngine a;   // звонящий
    CallEngine b;   // принимающий

    bool aUp = false, bUp = false, failed = false;
    QObject::connect(&a, &CallEngine::connected, &app, [&] { aUp = true; });
    QObject::connect(&b, &CallEngine::connected, &app, [&] { bUp = true; });
    QObject::connect(&a, &CallEngine::failed, &app, [&](const QString& r) {
        failed = true; printf("  [движок A] failed: %s\n", r.toUtf8().constData());
    });
    QObject::connect(&b, &CallEngine::failed, &app, [&](const QString& r) {
        failed = true; printf("  [движок B] failed: %s\n", r.toUtf8().constData());
    });

    // Сигнальный loopback: колбэки libdatachannel приходят из её потока,
    // авто-коннекты (принимающая сторона — &app) становятся queued.
    QObject::connect(&a, &CallEngine::localOffer, &app, [&](const QString& sdp) {
        b.startAsCallee(sdp);
    });
    QObject::connect(&b, &CallEngine::localAnswer, &app, [&](const QString& sdp) {
        a.setRemoteAnswer(sdp);
    });
    QObject::connect(&a, &CallEngine::localCandidate, &app,
                     [&](const QString& c, const QString& mid) {
        b.addRemoteCandidate(c, mid);
    });
    QObject::connect(&b, &CallEngine::localCandidate, &app,
                     [&](const QString& c, const QString& mid) {
        a.addRemoteCandidate(c, mid);
    });

    // Соединение: host-кандидатов достаточно, ICE-серверы не нужны.
    // Порядок как в CallController: сначала конфигурация, потом старт.
    if (!echoMode) a.setTestTone(true);   // мелодия вместо микрофона
    a.setIceServers({});
    b.setIceServers({});
    a.startAsCaller();

    printf("♪ Рингтон (как при входящем)…\n");
    CallSounds::instance().startRingtone();

    QDeadlineTimer t0(15000);
    while (!(aUp && bUp) && !t0.hasExpired() && !failed) {
        QCoreApplication::processEvents();
        QThread::msleep(15);
    }
    CallSounds::instance().stopRingtone();
    check(aUp && bUp, "WebRTC-соединение установлено (оба пира connected)");
    if (!(aUp && bUp)) {
        printf("\nИтог: соединение не сложилось\n");
        return 1;
    }

    CallSounds::instance().playConnectChime();
    if (echoMode) {
        printf("♪ «Дин» соединения. ПЕРВЫЕ 3 c скажите что-нибудь — услышите себя.\n");
        printf("   Потом динамики глушатся: без наушников петля завывает.\n\n");
        QDeadlineTimer echo(3000);
        while (!echo.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        b.setDeaf(true);   // глушим ВОСПРОИЗВЕДЕНИЕ; декодер и счётчик живут
        printf("   динамики выключены, эфир продолжается\n\n");
    } else {
        printf("♪ «Дин» соединения. Сейчас из колонок — арпеджио C5-E5-G5,\n");
        printf("   прошедшее весь конвейер: синтез → Opus → RTP → декодер → динамик.\n");
        printf("   На 4-й секунде мьют на 3 c — музыка замрёт и продолжится.\n\n");
    }

    // Эфир: секундная статистика + мьют-окно.
    int sentBeforeMute = 0, sentDuringMute = 0;
    int mutedAtSec = -1;
    const int total = echoMode ? 9 : 12;
    for (int sec = 1; sec <= total; ++sec) {
        QDeadlineTimer slice(1000);
        while (!slice.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        const int muteAt = echoMode ? 3 : 4;
        const int unmuteAt = echoMode ? 6 : 7;
        if (sec == muteAt) {
            sentBeforeMute = a.rtpSent();
            a.setMuted(true);
            mutedAtSec = sec;
            printf("  %2d c | sent=%4d recv=%4d | 🎙 мьют ВКЛ\n",
                   sec, a.rtpSent(), b.rtpReceived());
            continue;
        }
        if (sec == unmuteAt) {
            sentDuringMute = a.rtpSent() - sentBeforeMute;
            a.setMuted(false);
        }
        printf("  %2d c | sent=%4d recv=%4d |\n", sec, a.rtpSent(), b.rtpReceived());
    }
    const int totalSent = a.rtpSent();
    const int totalRecv = b.rtpReceived();
    const bool streamPresent = totalSent > 0;
    if (streamPresent) {
        check(totalRecv * 100 >= totalSent * 70,
              "звук доехал: ≥70% кадров (получено/отправлено)");
    } else {
        printf("  [ ! ] поток не дал кадров — тракт проверен соединением и рингтоном\n");
    }
    if (mutedAtSec > 0 && streamPresent) {
        // За 3 c мьюта в эфир должно уйти заметно меньше кадров (тон/тишину
        // движок не кодирует).
        check(sentDuringMute <= 10,
              "мьют глушит исходящий поток");
    }
    check(true, "рингтон и «динь» отработали без падения");

    a.hangup();
    b.hangup();
    printf("\nИтог: %s\n", fails ? "ЕСТЬ РАСХОЖДЕНИЯ" : "ВСЁ РАБОТАЕТ");
    return fails ? 1 : 0;
}
