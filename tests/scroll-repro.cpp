// scroll-repro.cpp — верификатор механики прокрутки истории вверх.
//
// Регрессия «листаешь вверх — сообщения странно себя ведут»: prepend-батчи
// вставлялись в ОБРАТНОМ порядке (инверсии), а smoothScrollTo оставлял
// programmaticScroll_ = true навсегда — догрузка по порогу v < 400 умирала.
//
// Offscreen, без сети: чат с 300 сообщениями на 10 дней, открытие, прокрутка
// вверх «колесом» (шаг 120px) и серия PgUp. Проверяем на каждом этапе:
//   - порядок msgId сверху вниз монотонный (0 инверсий);
//   - дыр между соседними строками нет (gap > 24px = «пустота» со скринов);
//   - растяжка одна, сверху;
//   - материализовано ожидаемое число бабблов.
//
// Запуск: QT_QPA_PLATFORM=offscreen ./build-linux/bin/scroll-repro
// Выход: 0 = всё ровно, 1 = есть расхождения.

#include "ui/ChatPage.h"
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Session.h"
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QFrame>
#include <QLabel>
#include <QLayout>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

static int g_failed = 0;

static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failed;
}

static QScrollArea* findMsgScroll(ChatPage* page) {
    const auto areas = page->findChildren<QScrollArea*>();
    for (QScrollArea* sa : areas)
        if (sa->widget() && sa->widget()->objectName() == QLatin1String("msgContainer"))
            return sa;
    return nullptr;
}

struct Stats { int rows = 0, bubbles = 0, inv = 0, holes = 0, stretchIdx = -1, nStretch = 0; };

static Stats dump(ChatPage* page, const QString& tag) {
    auto* sa   = findMsgScroll(page);
    auto* cont = sa->widget();
    auto* sb   = sa->verticalScrollBar();
    auto* lay  = cont->layout();

    struct RowInfo { int y, h; QString id; };
    QList<RowInfo> rows;
    QStringList order;
    Stats st;
    for (int i = 0; i < lay->count(); ++i) {
        QLayoutItem* it = lay->itemAt(i);
        if (!it->widget()) {
            if (st.stretchIdx < 0) st.stretchIdx = i;
            ++st.nStretch;
            continue;
        }
        QWidget* w = it->widget();
        QString id;
        for (QFrame* f : w->findChildren<QFrame*>()) {
            const QString mid = f->property("msgId").toString();
            if (!mid.isEmpty()) { id = mid; break; }
        }
        const QPoint p = w->mapTo(cont, QPoint(0, 0));
        rows.append({p.y(), w->height(), id});
        if (!id.isEmpty()) order << id;
    }
    st.rows = rows.size();
    st.bubbles = order.size();
    QString firstInv, firstHole;
    for (int i = 1; i < order.size(); ++i) {
        const int a = order[i - 1].mid(1).toInt(), b = order[i].mid(1).toInt();
        if (a > b) {
            ++st.inv;
            if (firstInv.isEmpty())
                firstInv = QStringLiteral("m%1 над m%2 (позиция %3)").arg(a).arg(b).arg(i);
        }
    }
    for (int i = 1; i < rows.size(); ++i) {
        const int gap = rows[i].y - (rows[i - 1].y + rows[i - 1].h);
        if (gap > 24) {
            ++st.holes;
            if (firstHole.isEmpty())
                firstHole = QStringLiteral("%1px после %2 (y=%3)")
                                .arg(gap)
                                .arg(rows[i - 1].id.isEmpty() ? QStringLiteral("[сепаратор]")
                                                              : rows[i - 1].id)
                                .arg(rows[i].y);
        }
    }
    printf("[%s] sb=%d/%d контейнер=%dpx строк=%d бабблов=%d stretch@%d×%d инверсий=%d дыр=%d\n",
           tag.toUtf8().constData(), sb->value(), sb->maximum(), cont->height(),
           st.rows, st.bubbles, st.stretchIdx, st.nStretch, st.inv, st.holes);
    fflush(stdout);
    if (!firstInv.isEmpty())  printf("    первая инверсия: %s\n", firstInv.toUtf8().constData());
    if (!firstHole.isEmpty()) printf("    первая дыра: %s\n", firstHole.toUtf8().constData());
    return st;
}

// Реальный кэш переписок (chatcache/*.bin): сортируем по createdAt, id = m0..mN
// в хронологическом порядке → инверсии в раскладке ловим штатной проверкой.
static QList<ChatMessage> loadCacheFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    const QJsonArray arr = doc.object().value(QStringLiteral("msgs")).toArray();
    struct Item { QString ca; ChatMessage m; };
    QList<Item> items;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Item it;
        it.ca = o.value(QStringLiteral("ca")).toString();
        it.m.createdAt = it.ca;
        it.m.content   = o.value(QStringLiteral("c")).toString();
        it.m.messageType = o.value(QStringLiteral("t")).toString(QStringLiteral("text"));
        it.m.time      = o.value(QStringLiteral("tm")).toString();
        it.m.sent      = o.value(QStringLiteral("out")).toBool();
        it.m.senderName= o.value(QStringLiteral("sn")).toString();
        it.m.filePath  = o.value(QStringLiteral("fp")).toString();
        it.m.fileName  = o.value(QStringLiteral("fn")).toString();
        it.m.fileSize  = static_cast<qint64>(o.value(QStringLiteral("fs")).toInteger());
        items.append(it);
    }
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.ca < b.ca; });
    QList<ChatMessage> out;
    for (int i = 0; i < items.size(); ++i) {
        ChatMessage m = items[i].m;
        m.id = QStringLiteral("m%1").arg(i);   // хронологический номер
        out.append(m);
    }
    return out;
}

// Полный цикл: открыть чат → прокрутить вверх до упора → вернуть статистику.
static Stats exerciseHistory(ChatPage* page, const QList<Chat>& chats,
                             const QList<ChatMessage>& msgs, const QString& tag) {
    // Вернуть текущий вид к низу: иначе applyMessages пойдёт в инкрементальную
    // ветку (!stickBottom_) и не перерисует чат под новые данные.
    {
        auto* sb0 = findMsgScroll(page)->verticalScrollBar();
        for (int i = 0; i < 40; ++i) QCoreApplication::processEvents();   // дождаться анимаций
        sb0->setValue(sb0->maximum());
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    }
    page->injectForDesignTest(chats, QList<Folder>(), chats.first().id, msgs);
    for (int i = 0; i < 400; ++i) QCoreApplication::processEvents();
    auto* sa = findMsgScroll(page);
    auto* sb = sa->verticalScrollBar();
    for (int step = 0; step < 400; ++step) {
        if (sb->value() == 0) sb->setValue(qMin(400, sb->maximum()));
        sb->setValue(sb->value() - 120);
        for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
    }
    return dump(page, tag);
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherDesignTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ScrollRepro"));

    Session::instance().token = QStringLiteral("offscreen_test_token");
    Session::instance().userId = QStringLiteral("offscreen_user");

    ApiClient api;
    WsClient ws;
    ChatPage page(&api, &ws);
    page.resize(1280, 800);
    page.show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    QList<Chat> chats;
    Chat alice;
    alice.id = QStringLiteral("u_alice");
    alice.displayName = QStringLiteral("Алиса");
    alice.online = true;
    chats.append(alice);

    // 300 сообщений на 10 дней (по 30 в день), id растут хронологически: m0..m299.
    QList<ChatMessage> msgs;
    for (int i = 0; i < 300; ++i) {
        ChatMessage m;
        m.id = QStringLiteral("m%1").arg(i);
        m.content = QStringLiteral("текст сообщения %1").arg(i);
        m.sent = (i % 3 == 0);
        m.senderName = m.sent ? QString() : QStringLiteral("Алиса");
        const int day = 5 + i / 30;   // 2026-09-05 .. 2026-09-14
        m.createdAt = QStringLiteral("2026-09-%1T10:%2:00")
                          .arg(day, 2, 10, QChar('0')).arg(i % 60, 2, 10, QChar('0'));
        m.time = QStringLiteral("10:%1").arg(i % 60, 2, 10, QChar('0'));
        msgs.append(m);
    }

    printf("1) Открытие чата с историей (хвост 50, продолжение чанками)\n");
    page.injectForDesignTest(chats, QList<Folder>(), QStringLiteral("u_alice"), msgs);
    for (int i = 0; i < 400; ++i) QCoreApplication::processEvents();   // continueTail + smoothScrollTo
    {
        const Stats st = dump(&page, QStringLiteral("после открытия"));
        check(st.inv == 0, "после открытия: порядок хронологический (0 инверсий)");
        check(st.holes == 0, "после открытия: дыр нет");
        check(st.bubbles == 50, "после открытия: материализован хвост 50");
        check(st.nStretch == 1 && st.stretchIdx == 0, "после открытия: одна растяжка сверху");
    }

    auto* sa = findMsgScroll(&page);
    auto* sb = sa->verticalScrollBar();

    printf("2) Прокрутка вверх «колесом» до самого верха\n");
    for (int step = 0; step < 400; ++step) {
        // На «дне» шага нет события valueChanged — шевелим значение, чтобы
        // цепочка prepend'ов (порог v < 400) продолжила достройку.
        if (sb->value() == 0) sb->setValue(qMin(400, sb->maximum()));
        sb->setValue(sb->value() - 120);
        for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
    }
    {
        const Stats st = dump(&page, QStringLiteral("после прокрутки вверх"));
        check(st.inv == 0, "после прокрутки: порядок хронологический (0 инверсий)");
        check(st.holes == 0, "после прокрутки: дыр нет");
        check(st.bubbles == 300, "вся история материализована (300 бабблов)");
        check(st.nStretch == 1 && st.stretchIdx == 0, "после прокрутки: одна растяжка сверху");
    }

    printf("3) Переоткрытие + серия PgUp (прыжки страницей)\n");
    page.injectForDesignTest(chats, QList<Folder>(), QStringLiteral("u_alice"), msgs);
    for (int i = 0; i < 400; ++i) QCoreApplication::processEvents();
    for (int step = 0; step < 30; ++step) {
        sb->setValue(sb->value() - sb->pageStep());
        for (int i = 0; i < 8; ++i) QCoreApplication::processEvents();
    }
    {
        const Stats st = dump(&page, QStringLiteral("после PgUp-серии"));
        check(st.inv == 0, "после PgUp: порядок хронологический (0 инверсий)");
        check(st.holes == 0, "после PgUp: дыр нет");
    }

    printf("%s\n", g_failed ? "ЕСТЬ РАСХОЖДЕНИЯ" : "ВСЁ РОВНО");

    // ── Режим реальных данных: прогон всех живых кэшей переписок ──────────────
    if (argc > 1) {
        printf("4) Реальные кэши переписок: %s\n", argv[1]);
        const QDir dir(QString::fromUtf8(argv[1]));
        const auto bins = dir.entryList({QStringLiteral("*.bin")}, QDir::Files, QDir::Name);
        int done = 0;
        for (const QString& name : bins) {
            const QList<ChatMessage> msgs = loadCacheFile(dir.filePath(name));
            if (msgs.size() < 3) continue;
            // Уникальный id чата на прогон: тот же id → openChat не перерисует,
            // и incremental-ветка applyMessages оставит старые бабблы.
            QList<Chat> c1;
            Chat probe = chats.first();
            probe.id = QStringLiteral("u_cache_%1").arg(done);
            c1.append(probe);
            const Stats st = exerciseHistory(&page, c1, msgs,
                                             QStringLiteral("кэш %1 (n=%2)").arg(name.left(10)).arg(msgs.size()));
            const bool ok = st.inv == 0 && st.holes == 0 && st.bubbles == msgs.size();
            char buf[256];
            snprintf(buf, sizeof buf, "кэш %s: n=%d бабблов=%d инверсий=%d дыр=%d",
                     name.left(10).toUtf8().constData(), msgs.size(), st.bubbles, st.inv, st.holes);
            check(ok, buf);
            // Живые рендеры (визуальное доказательство): верх и середина истории.
            page.grab().save(QStringLiteral("/tmp/scroll-proof-%1-top.png").arg(name.left(6)));
            {
                auto* sb1 = findMsgScroll(&page)->verticalScrollBar();
                sb1->setValue(sb1->maximum() / 2);
                for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
                page.grab().save(QStringLiteral("/tmp/scroll-proof-%1-mid.png").arg(name.left(6)));
            }
            ++done;
        }
        check(done > 0, "найден хотя бы один кэш для прогона");
        printf("%s\n", g_failed ? "ЕСТЬ РАСХОЖДЕНИЯ" : "ВСЁ РОВНО");
    }

    return g_failed ? 1 : 0;
}
