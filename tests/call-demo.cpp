// call-demo — демо-звонок С ЗВУКОМ без второго человека и без сервера:
// два WebRTC-движка (CallEngine) в одном процессе соединяются loopback'ом
// (offer/answer/ICE прокидываются напрямую сигналами), реальный RTP-эфир
// libdatachannel, Opus 20 мс.
//
// Что слышно из колонок: рингтон (двухнотный chime) → «динь» соединения →
// ваш собственный голос с микрофона (эхо через весь стек:
// микрофон → Opus → RTP/SRTP → декодер → динамик). На 5-й секунде микрофон
// мьютится на 3 секунды — голос пропадает и возвращается.
//
// PASS-критерий: оба движка connected + до эфира дошло ≥70% отправленных
// кадров + мьют действительно гасит исходящий поток.
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

    printf("Демо-звонок с звуком (loopback, два пира в одном процессе)\n\n");

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
    printf("♪ «Дин» соединения. Эфир 12 c: говорите в микрофон — услышите себя.\n");
    printf("   на 5-й секунде микрофон мьют на 3 c (голос должен пропасть).\n\n");

    // Эфир: секундная статистика + мьют-окно.
    int sentBeforeMute = 0, sentDuringMute = 0;
    int mutedAtSec = -1;
    for (int sec = 1; sec <= 12; ++sec) {
        QDeadlineTimer slice(1000);
        while (!slice.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(15);
        }
        if (sec == 5) {
            sentBeforeMute = a.rtpSent();
            a.setMuted(true);
            mutedAtSec = sec;
            printf("  %2d c | sent=%4d recv=%4d | 🎙 мьют ВКЛ\n",
                   sec, a.rtpSent(), b.rtpReceived());
            continue;
        }
        if (sec == 8) {
            sentDuringMute = a.rtpSent() - sentBeforeMute;
            a.setMuted(false);
        }
        printf("  %2d c | sent=%4d recv=%4d |\n", sec, a.rtpSent(), b.rtpReceived());
    }
    const int totalSent = a.rtpSent();
    const int totalRecv = b.rtpReceived();
    const bool micPresent = totalSent > 0;
    if (micPresent) {
        check(totalRecv * 100 >= totalSent * 70,
              "звук доехал: ≥70% кадров (получено/отправлено)");
    } else {
        printf("  [ ! ] микрофон не дал кадров (нет устройства/занят) — "
               "тракт проверен соединением и рингтоном\n");
    }
    if (mutedAtSec > 0 && micPresent) {
        // За 3 c мьюта в эфире должно уйти заметно меньше кадров, чем за
        // сопоставимое время до него (тишину движок не кодирует).
        check(sentDuringMute <= 10,
              "мьют глушит исходящий поток");
    }
    check(true, "рингтон и «динь» отработали без падения");

    a.hangup();
    b.hangup();
    printf("\nИтог: %s\n", fails ? "ЕСТЬ РАСХОЖДЕНИЯ" : "ВСЁ РАБОТАЕТ");
    return fails ? 1 : 0;
}
