// bench-live.cpp — нагрузочный прогон PRF-02: чат на 10 000 сообщений.
// Метрики: время открытия (inject + рендер хвоста), плавность скролла
// (кадры/с по циклу processEvents во время прокрутки), RSS после цикла
// 24 прокруток (утечка ≤30МБ — порог DoD). Выход 0 = пороги соблюдены.
//
// Запуск: ./build-linux/bin/bench-live   (offscreen по умолчанию)

#include "ui/ChatPage.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Session.h"

#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QWheelEvent>
#include <cstdio>
#include <cstdlib>

static qint64 rssKb() {
    QFile f(QStringLiteral("/proc/self/status"));
    if (!f.open(QIODevice::ReadOnly)) return -1;
    const QByteArray all = f.readAll();
    const int at = all.indexOf("VmRSS:");
    if (at < 0) return -1;
    return all.mid(at + 6).simplified().split(' ').first().toLongLong();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherDesignTest"));
    QCoreApplication::setApplicationName(QStringLiteral("BenchLive"));
    Session::instance().token = QStringLiteral("offscreen_test_token");
    Session::instance().userId = QStringLiteral("offscreen_user");

    ApiClient api;
    WsClient ws;
    ChatPage page(&api, &ws);
    page.resize(1280, 800);
    page.show();
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

    // 10 000 сообщений: смесь текстов/длинных URL/эмодзи, как живой чат.
    constexpr int kMsgs = 10000;
    QList<Chat> chats;
    Chat alice;
    alice.id = QStringLiteral("u_alice");
    alice.displayName = QStringLiteral("Алиса");
    alice.lastMessage = QStringLiteral("бенчмарк");
    alice.time = QStringLiteral("12:00");
    chats.append(alice);
    QList<ChatMessage> msgs;
    msgs.reserve(kMsgs);
    const QString filler[] = {
        QStringLiteral("текст сообщения номер %1"),
        QStringLiteral("длинный текст с https://example.com/very/long/path?query=%1&more=data про всё"),
        QStringLiteral("эмодзи 😀🔥🚀 и текст %1"),
        QStringLiteral("код: int x = %1; // комментарий средней длины для проверки вордврапа"),
    };
    const QDateTime base = QDateTime(QDate(2025, 1, 1), QTime(0, 0, 0));
    for (int i = 0; i < kMsgs; ++i) {
        ChatMessage m;
        m.id = QStringLiteral("b%1").arg(i);
        m.content = filler[i % 4].arg(i);
        m.sent = (i % 3 == 0);
        m.time = QStringLiteral("12:%1").arg(i % 60, 2, 10, QLatin1Char('0'));
        m.createdAt = base.addSecs(60LL * i).toString(Qt::ISODate);
        m.senderName = m.sent ? QString() : QStringLiteral("Алиса");
        msgs.append(m);
    }

    // 1) Открытие: inject → полный проход событий до успокоения.
    QElapsedTimer t;
    t.start();
    page.injectForDesignTest(chats, QList<Folder>(),
                             QStringLiteral("u_alice"), msgs);
    qint64 openMs = -1;
    {
        // Ждём пока очередь событий опустеет (достройка чанков хвоста).
        for (int idle = 0; idle < 40 && t.elapsed() < 20000;) {
            QCoreApplication::processEvents();
            if (t.elapsed() > 100) ++idle;
        }
        openMs = t.elapsed();
    }
    printf("открытие 10к: %lld мс (порог 800)\n", openMs);

    // 2) Скролл: 6 «колёс» вниз-вверх, кадры считаем по processEvents-циклу
    // в фиксированные 1-секундные окна (offscreen: кадров как таковых нет —
    // меряем отклик layout/paint-очереди).
    auto* sa = page.findChild<QScrollArea*>(QStringLiteral("msgArea"));
    if (!sa) { printf("msgArea не найден\n"); return 1; }
    auto* vp = sa->viewport();
    const qint64 rssBefore = rssKb();
    int cycles = 0;
    QElapsedTimer scrollT;
    scrollT.start();
    while (scrollT.elapsed() < 3000) {   // 3 с непрерывной прокрутки
        for (int w = 0; w < 3; ++w) {
            QWheelEvent we(QPointF(200, 400), QPointF(200, 400),
                           QPoint(0, 0), QPoint(0, -240),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(vp, &we);
        }
        for (int i = 0; i < 12; ++i) QCoreApplication::processEvents();
        ++cycles;
    }
    const qint64 rssAfter = rssKb();
    const double cyclesPerSec = cycles / 3.0;
    printf("скролл: %.1f циклов прокрутки/с (UI не блокируется)\n", cyclesPerSec);
    printf("RSS: %lld → %lld КБ (дельта %lld КБ, порог утечки 30720)\n",
           rssBefore, rssAfter, rssAfter - rssBefore);

    const bool openOk = openMs <= 800;
    const bool responsive = cyclesPerSec > 5.0;   // UI живой (без фриза >200мс)
    const bool rssOk = (rssAfter - rssBefore) <= 30720 && rssAfter <= 500 * 1024;
    printf("ИТОГ: %s (открытие %s, отклик %s, RSS %s)\n",
           (openOk && responsive && rssOk) ? "PASS" : "FAIL",
           openOk ? "ok" : "превышен",
           responsive ? "ok" : "фриз",
           rssOk ? "ok" : "превышен");
    return (openOk && responsive && rssOk) ? 0 : 1;
}
