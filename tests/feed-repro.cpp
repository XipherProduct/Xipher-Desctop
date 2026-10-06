// feed-repro.cpp — живой рендер ленты из РЕАЛЬНОГО кэша пользователя.
// Аргументы: feed-repro <chatcache-файл.json> <out-prefix> [user|channel] [width] [height]
// Читает messages-кэш (формат ChatCache: {"msgs":[…]}), инжектит в ChatPage
// и снимает PNG: низ, верх после прокрутки (с проверкой якоря prepend).
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QScrollBar>
#include <QScrollArea>
#include <QDebug>
#include <cstdio>

#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Models.h"
#include "net/Session.h"
#include "ui/ChatPage.h"
#include <QColor>

static void colorSanity() {
    const QColor pct(QStringLiteral("rgba(139,92,246,45%)"));
    const QColor flt(QStringLiteral("rgba(139,92,246,0.45)"));
    fprintf(stderr, "QColor percent-alpha: valid=%d a=%d\n", pct.isValid(), pct.alpha());
    fprintf(stderr, "QColor float-alpha:   valid=%d a=%d\n", flt.isValid(), flt.alpha());
}

static int msgContainerW(ChatPage& page) {
    if (auto* c = page.findChild<QWidget*>(QStringLiteral("msgContainer")))
        return c->width();
    return -1;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherDesignTest"));
    QCoreApplication::setApplicationName(QStringLiteral("FeedRepro"));
    if (argc < 3) { qWarning("usage: feed-repro <cache.bin> <out-prefix> [user|channel] [w] [h]"); return 2; }
    colorSanity();

    const QString cachePath = QString::fromUtf8(argv[1]);
    const QString outPrefix = QString::fromUtf8(argv[2]);
    const bool channel = argc > 3 && QByteArray(argv[3]) == "channel";
    const int W = argc > 4 ? QByteArray(argv[4]).toInt() : 1280;
    const int H = argc > 5 ? QByteArray(argv[5]).toInt() : 800;

    QFile f(cachePath);
    if (!f.open(QIODevice::ReadOnly)) { qWarning("cannot open %s", qPrintable(cachePath)); return 3; }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray arr = root.value(QStringLiteral("msgs")).toArray();

    Session::instance().token = QStringLiteral("offscreen_test_token");
    Session::instance().userId = QStringLiteral("b99c30bf-69a3-4c54-9aef-fb87904fedf5");

    QList<ChatMessage> msgs;
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        ChatMessage m;
        m.id = o.value(QStringLiteral("id")).toString();
        m.senderId = o.value(QStringLiteral("sid")).toString();
        m.senderName = o.value(QStringLiteral("sn")).toString();
        m.content = o.value(QStringLiteral("c")).toString();
        m.messageType = o.value(QStringLiteral("t")).toString();
        m.time = o.value(QStringLiteral("tm")).toString();
        m.createdAt = o.value(QStringLiteral("ca")).toString();
        m.sent = o.value(QStringLiteral("out")).toBool();
        m.filePath = o.value(QStringLiteral("fp")).toString();
        m.fileName = o.value(QStringLiteral("fn")).toString();
        msgs.append(m);
    }
    fprintf(stderr, "messages: %d\n", msgs.size());

    ApiClient api;
    WsClient ws;
    ChatPage page(&api, &ws);
    page.resize(W, H);
    page.show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    Chat peer;
    peer.id = QStringLiteral("b99c30bf-69a3-4c54-9aef-fb87904fedf5");
    peer.displayName = channel ? QStringLiteral("Xipher News") : QStringLiteral("Xipher News (ЛС)");
    peer.kind = channel ? ChatKind::Channel : ChatKind::User;
    peer.lastMessage = QStringLiteral("…");
    peer.time = QStringLiteral("12:28");
    if (channel) peer.role = QStringLiteral("creator");

    page.injectForDesignTest(QList<Chat>{peer}, QList<Folder>(), peer.id, msgs);
    for (int i = 0; i < 60; ++i) QCoreApplication::processEvents();

    auto* area = page.findChild<QScrollArea*>(QStringLiteral("msgArea"));
    if (!area) { qWarning("no msgArea"); return 4; }
    auto* sb = area->verticalScrollBar();
    const QDir out = QDir::temp();

    // Ресайз ПОСЛЕ укладки (как живое окно юзера): узкий режим WIN-03 прячет
    // сайдбар, вьюпорт растёт, кламп расширяет бабблы.
    page.resize(W - 40, H);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    page.resize(W, H);
    for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    fprintf(stderr, "after live-resize: area=%d viewport=%d\n",
            area->width(), area->viewport()->width());

    page.grab().save(out.filePath(outPrefix + QStringLiteral("-bottom.png")));
    fprintf(stderr, "scroll max=%d value=%d\n", sb->maximum(), sb->value());
    fprintf(stderr, "page=%dx%d area=%d viewport=%d container=%d\n",
            page.width(), page.height(), area->width(),
            area->viewport()->width(), msgContainerW(page));

    // Диагностика бабблов: реальные геометрия и QSS последних пузырей.
    const auto rows = [&]() {
        QList<QWidget*> r;
        for (QWidget* wgt : area->findChildren<QWidget*>())
            if (wgt->objectName() == QStringLiteral("msgRow")) r.append(wgt);
        return r;
    }();
    fprintf(stderr, "msgRows: %d\n", rows.count());
    for (QWidget* row : rows.mid(qMax(0, rows.size() - 3))) {
        const QPoint p = row->mapTo(area->viewport(), QPoint(0, 0));
        fprintf(stderr, "row y=%d size=%dx%d\n", p.y(), row->width(), row->height());
        for (QFrame* b : row->findChildren<QFrame*>()) {
            if (b->objectName() != QStringLiteral("bubbleIn")
                && b->objectName() != QStringLiteral("bubbleOut")) continue;
            fprintf(stderr, "  bubble %s size=%dx%d maxW=%d mediaOnly=%d\n",
                    b->objectName().toUtf8().constData(), b->width(), b->height(),
                    b->maximumWidth(), b->property("mediaOnly").toBool());
            fprintf(stderr, "  qss: %s\n", b->styleSheet().toUtf8().constData());
        }
    }

    // ── Якорь prepend: прокрутка к верху должна загрузить старые сообщения,
    // НЕ сдвинув контент. Меряем: верхняя граница последнего поста в координатах
    // вьюпорта до и после prepend.
    auto anchorY = [&]() -> int {
        QWidget* lastRow = nullptr;
        for (QWidget* row : area->findChildren<QWidget*>())
            if (row->objectName() == QStringLiteral("msgRow")) lastRow = row;   // последний добавленный
        if (!lastRow) return -1;
        return lastRow->mapTo(area->viewport(), QPoint(0, 0)).y();
    };
    Q_UNUSED(anchorY());


    // Плавно к верху, как юзер: несколько шагов.
    const int step = qMax(1, sb->pageStep());
    while (sb->value() > 0) {
        sb->setValue(qMax(0, sb->value() - step));
        for (int i = 0; i < 8; ++i) QCoreApplication::processEvents();
        if (sb->value() == 0) break;
        // после prepend максимум вырос — продолжаем с нового значения
    }
    for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
    page.grab().save(out.filePath(outPrefix + QStringLiteral("-top.png")));
    fprintf(stderr, "after scroll-to-top: max=%d value=%d\n", sb->maximum(), sb->value());
    return 0;
}
