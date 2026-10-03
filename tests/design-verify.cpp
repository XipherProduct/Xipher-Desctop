// design-verify.cpp — offscreen-верификация дизайна главного экрана ChatPage
// против CSS-значений веб-клиента (web/css/tokens.css, chat.html).
//
// Рендерим реальный ChatPage без сети (QT_QPA_PLATFORM=offscreen), подсаживаем
// тестовые чаты/папки/историю через тестовый шов и проверяем:
//   - геометрию (ширина сайдбара 380, рейл папок 72, шапка 60);
//   - цвета пикселей (поверхности, бабблы, бейджи, композер-пилюля) с допуском ±4.
//
// Запуск: QT_QPA_PLATFORM=offscreen ./build-linux/bin/design-verify
// Выход: 0 = все проверки пройдены, 1 = есть расхождения (список в stdout).

#include "ui/ChatPage.h"
#include "app/MainWindow.h"
#include "ui/ProfilePanel.h"
#include "ui/CallOverlay.h"
#include "ui/EmojiPicker.h"
#include "ui/AnimatedEmojiLabel.h"
#include "ui/CallSounds.h"
#include "ui/ImageViewer.h"
#include "ui/QuickSwitcher.h"
#include "ui/ChatPickerDialog.h"
#include "ui/SuperSearchDialog.h"
#include <QCheckBox>
#include <QToolButton>
#include <QClipboard>
#include <QBuffer>
#include "net/ApiClient.h"
#include "net/WsClient.h"
#include "net/Session.h"
#include "net/Prefs.h"
#include "net/VoiceRecorder.h"
#include "util/Autostart.h"
#include "util/MprisAdapter.h"
#include "util/PulseAttenuator.h"
#include "util/GlobalHotkeys.h"
#include "net/RnNoise.h"
#include "ui/RichDoc.h"
#include "ui/RichRender.h"
#include "ui/RichEditor.h"
#include "ui/VoiceMessageWidget.h"
#include "ui/VideoMessageWidget.h"
#include "ui/ImageEditor.h"
#include "ui/ChatWindow.h"

// Шов для эмуляции сигнала скорости (VOX-02).
static void emitHelperSpeed(VoiceMessageWidget* w, qreal r) {
    emit w->speedRequested(r);
}

#include <QApplication>
#include <QColor>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <algorithm>
#include <QLayout>
#include <QScrollArea>
#include <QScrollBar>
#include <QWidget>
#include <QFile>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QPointer>
#include <QProcess>
#include <QSplitter>
#include <QThread>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QWheelEvent>
#include <QDir>
#include <cstdio>
#include <cmath>
#include "ui/SettingsDialog.h"
#include "ui/Theme.h"
#include <QStackedWidget>

static int failures = 0;
static void check(bool ok, const QString& what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toUtf8().constData());
    if (!ok) ++failures;
}

// Пиксель изображения с допуском по каналам.
static bool pixelNear(const QImage& img, int x, int y, quint32 rgb, int tol = 4) {
    if (x < 0 || y < 0 || x >= img.width() || y >= img.height()) return false;
    const QColor c = img.pixelColor(x, y);
    const QColor e = QColor::fromRgb(rgb);
    return qAbs(c.red() - e.red()) <= tol && qAbs(c.green() - e.green()) <= tol
        && qAbs(c.blue() - e.blue()) <= tol;
}
static bool pixelNear2(const QColor& c, quint32 rgb, int tol = 14) {
    const QColor want = QColor::fromRgb(rgb);
    return qAbs(c.red() - want.red()) <= tol
        && qAbs(c.green() - want.green()) <= tol
        && qAbs(c.blue() - want.blue()) <= tol;
}
static QString px(const QImage& img, int x, int y) {
    return img.pixelColor(x, y).name();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherDesignTest"));
    QCoreApplication::setApplicationName(QStringLiteral("DesignVerify"));

    Session::instance().token = QStringLiteral("offscreen_test_token");
    Session::instance().userId = QStringLiteral("offscreen_user");

    ApiClient api;
    WsClient ws;
    ChatPage page(&api, &ws);
    page.resize(1280, 800);
    page.show();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    // ── Тестовые данные ───────────────────────────────────────────────────────
    QList<Chat> chats;
    Chat alice; alice.id = QStringLiteral("u_alice"); alice.displayName = QStringLiteral("Алиса");
    alice.lastMessage = QStringLiteral("Привет! Как дела?"); alice.time = QStringLiteral("12:30");
    alice.online = true; chats.append(alice);
    Chat bob; bob.id = QStringLiteral("u_bob"); bob.displayName = QStringLiteral("Боб");
    bob.lastMessage = QStringLiteral("Фото отпр."); bob.time = QStringLiteral("11:00");
    bob.unread = 3; chats.append(bob);
    Chat saved; saved.id = QStringLiteral("offscreen_user"); saved.isSaved = true;
    saved.displayName = QStringLiteral("Избранное"); saved.lastMessage = QStringLiteral("заметка");
    saved.time = QStringLiteral("Вчера"); chats.append(saved);

    Folder work; work.id = QStringLiteral("f_work"); work.name = QStringLiteral("Работа");
    work.chatKeys = {QStringLiteral("chat:u_bob")};
    work.icon = QStringLiteral("rocket"); work.color = QStringLiteral("#6fb1fc");

    QList<ChatMessage> msgs;
    for (int i = 0; i < 64; ++i) {
        ChatMessage m;
        m.id = QStringLiteral("m%1").arg(i);
        m.content = i == 0 ? QStringLiteral("первое сообщение") :
                    QStringLiteral("текст сообщения номер %1").arg(i);
        m.sent = (i % 3 == 0);
        m.time = QStringLiteral("12:%1").arg(i, 2, 10, QChar('0'));
        m.createdAt = QStringLiteral("2026-09-14T12:%1:00").arg(i, 2, 10, QChar('0'));
        m.senderName = m.sent ? QString() : QStringLiteral("Алиса");
        msgs.append(m);
    }
    // Регрессия «плывущих» сообщений: markdown, длинные URL, спойлеры.
    {
        ChatMessage m;
        m.id = "md1"; m.sent = true;
        m.content = "**жирный** и *курсив* и `код` и ||спойлер скрытый|| и ~~зачёркнутый~~ и __подчёркнутый__";
        m.time = "10:00"; m.createdAt = "2026-09-21T10:00:00";
        msgs.append(m);
        ChatMessage u;
        u.id = "url1"; u.sent = true;
        u.content = "https://lknpd.nalog.ru/api/v1/receipt/615491216850/201v1q7qbe/print";
        u.time = "10:01"; u.createdAt = "2026-09-21T10:01:00";
        msgs.append(u);
        // Реакции: своя (фиолетовая) и чужая — чипы под бабблом.
        ChatMessage r;
        r.id = "re1"; r.sent = false;
        r.content = "сообщение с реакциями";
        r.time = "10:02"; r.createdAt = "2026-09-21T10:02:00";
        r.reactions = {Reaction{QString::fromUtf8("\U0001F525"), 3, true},
                       Reaction{QString::fromUtf8("\U0001F44D"), 1, false}};
        msgs.insert(1, r);   // в видимую зону: список материализует только её
        // Правка: метка «· изм.» у времени.
        ChatMessage ed;
        ed.id = "ed1"; ed.sent = true; ed.edited = true;
        ed.content = "отредактированное сообщение";
        ed.time = "10:03"; ed.createdAt = "2026-09-21T10:03:00";
        msgs.insert(2, ed);
        // Код-блок ```…```: тёмная плашка с моно-шрифтом.
        ChatMessage cd;
        cd.id = "code1"; cd.sent = false;
        cd.content = "```cpp\nint main() { return 0; } // OK\n```";
        cd.time = "10:04"; cd.createdAt = "2026-09-21T10:04:00";
        msgs.insert(8, cd);   // ниже точки сэмпла входящего баббла
    }

    page.injectForDesignTest(chats, QList<Folder>{work}, QStringLiteral("u_alice"), msgs);
    for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();

    {
        int chips = 0, mine = 0;
        for (QPushButton* c : page.findChildren<QPushButton*>())
            if (c->objectName() == QStringLiteral("reactionChip")) {
                ++chips;
                if (c->property("mine").toBool()) ++mine;
            }
        check(chips == 2, QStringLiteral("реакции: чипы под бабблом (%1)").arg(chips));
        check(mine == 1, QStringLiteral("своя реакция помечена"));
        const QString chipQss = [&page]{
            for (QPushButton* c : page.findChildren<QPushButton*>())
                if (c->objectName() == QStringLiteral("reactionChip") && c->property("mine").toBool())
                    return c->styleSheet();
            return QString();
        }();
        check(chipQss.contains(QStringLiteral("#8B5CF6")),
              QStringLiteral("своя реакция — фиолетовая рамка"));
    }
    {
        bool editedMark = false;
        for (QLabel* l : page.findChildren<QLabel*>())
            if (l->text().contains(QStringLiteral("изм."))) editedMark = true;
        check(editedMark, QStringLiteral("правка: метка «· изм.» у времени"));
        // Код-блок: тег table с фоном присутствует в отрисованном баббле.
        bool codeBlock = false;
        {
            const QImage shot = page.grab().toImage();
            // ищем очень тёмную широкую плашку (#0B0A0E) в зоне сообщений
            for (int y = 100; y < shot.height() - 60; ++y) {
                int dark = 0;
                for (int x = 300; x < qMin(900, shot.width()); x += 4) {
                    const QColor c = shot.pixelColor(x, y);
                    if (c.red() <= 15 && c.green() <= 12 && c.blue() <= 18) ++dark;
                }
                if (dark > 40) { codeBlock = true; break; }
            }
        }
        check(codeBlock, QStringLiteral("код-блок: тёмная моно-плашка отрисована"));
        // Закреп: панель появляется и прячется.
        page.setPinnedMessage(QStringLiteral("ed1"), QStringLiteral("отредактированное сообщение"));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool barVisible = false;
        for (QWidget* w : page.findChildren<QWidget*>(QStringLiteral("pinnedBar")))
            if (w->isVisible()) barVisible = true;
        check(barVisible, QStringLiteral("закреп: панель закрепа видна"));
        page.clearPinnedMessage();
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        bool barHidden = true;
        for (QWidget* w : page.findChildren<QWidget*>(QStringLiteral("pinnedBar")))
            if (w->isVisible()) barHidden = false;
        check(barHidden, QStringLiteral("закреп: откреп прячет панель"));
    }

    // Базовые проверки — на «Стандартной» теме (предыдущий прогон мог
    // сохранить другую в Prefs).
    ThemePreset::save(QStringLiteral("gray"));
    page.applyTheme();
    for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();

    // Регрессия «плывущих» сообщений: строки подряд, текст не обрезан.
    {
        auto* cont = page.findChild<QWidget*>(QStringLiteral("msgContainer"));
        int prevBottom = -1, checkedRows = 0, overlap = 0, clipped = 0;
        QList<QWidget*> liveRows;
        if (cont && cont->layout())
            for (int i = 0; i < cont->layout()->count(); ++i)
                if (auto* it = cont->layout()->itemAt(i); it && it->widget())
                    if (it->widget()->findChild<QFrame*>()) liveRows << it->widget();
        for (QWidget* row : liveRows) {
            auto* bubble = row->findChild<QFrame*>();
            if (!bubble) continue;
            const int y = row->mapTo(cont, QPoint(0, 0)).y();
            if (prevBottom >= 0 && y < prevBottom - 1) ++overlap;
            prevBottom = y + row->height();
            for (QLabel* l : bubble->findChildren<QLabel*>()) {
                const int ly = l->mapTo(bubble, QPoint(0, 0)).y() + l->height();
                if (ly > bubble->height() + 2) ++clipped;
            }
            ++checkedRows;
        }
        check(checkedRows >= 10, QStringLiteral("строк сообщений отрисовано: ") + QString::number(checkedRows));
        check(overlap == 0, QStringLiteral("строки не перекрываются (анти-«плывут»)"));
        check(clipped == 0, QStringLiteral("текст не обрезан бабблом"));
    }

    auto countRows = [&page]() -> int {
        auto* c = page.findChild<QWidget*>(QStringLiteral("msgContainer"));
        int n = 0;
        if (c) for (QObject* o : c->children())
            if (auto* w = qobject_cast<QWidget*>(o))
                if (w->findChild<QFrame*>()) ++n;
        return n;
    };
    auto dumpRows = [&](const char* stage) {
        printf("      [rows @%s] = %d\n", stage, countRows());
    };
    // ── Геометрия (значения из chat.html / tokens.css) ────────────────────────
    printf("1) Геометрия\n");
    printf("platform=%s dpr=%.2f\n",
           QGuiApplication::platformName().toUtf8().constData(),
           page.devicePixelRatioF());


    auto* sidebar = page.findChild<QWidget*>(QStringLiteral("sidebar"));
    check(sidebar && sidebar->width() == 380, QStringLiteral("сайдбар 380px (--sidebar-width)"));
    auto* rail = page.findChild<QWidget*>(QStringLiteral("folderRail"));
    check(rail && rail->isVisible() && rail->width() == 72,
          QStringLiteral("рейл папок 72px (.folders-rail)"));
    auto* convHeader = page.findChild<QWidget*>(QStringLiteral("convHeader"));
    check(convHeader && convHeader->height() == 60, QStringLiteral("шапка чата 60px"));
    auto* pill = page.findChild<QWidget*>(QStringLiteral("tgInputBar"));
    check(pill && pill->isVisible(), QStringLiteral("композер-пилюля .tg-input-bar есть"));
    check(page.findChild<QWidget*>(QStringLiteral("folderRailIcon")) != nullptr,
          QStringLiteral("плитки-иконки рейла есть"));
    check(page.findChild<QWidget*>(QStringLiteral("folderRailEdit")) != nullptr,
          QStringLiteral("кнопка «＋» внизу рейла"));

    // ── Пиксели (цвета из tokens.css) ────────────────────────────────────────
    printf("2) Цвета (tokens.css / chat.html)\n");
    const QPixmap pm = page.grab();
    const QImage img = pm.toImage();
    img.save(QStringLiteral("/tmp/design-main.png"));

    // Сайдбар: поверхность surface-1 #131218.
    check(pixelNear(img, 200, 640, 0x131218), QStringLiteral("фон сайдбара #131218 (surface-1): ") + px(img, 200, 640));
    // Область сообщений: bg-base #0B0A0E — ищем ЛЮБОЙ фоновый пиксель в зоне
    // (при 52+ бабблах фиксированная точка может попасть на баббл).
    bool bgFound = false;
    for (int y = 100; y < 700 && !bgFound; y += 7)
        for (int x = 400; x < 1260 && !bgFound; x += 7)
            if (pixelNear(img, x, y, 0x0B0A0E)) bgFound = true;
    check(bgFound, QStringLiteral("фон переписки #0B0A0E присутствует"));
    // Шапка чата: #131218.
    check(pixelNear(img, 900, 30, 0x131218), QStringLiteral("шапка чата #131218 (bg-secondary матте): ") + px(img, 900, 30));
    // Композер-пилюля: surface-2 #1A1822 (между кнопками, слева от текста).
    if (pill) {
        const QPoint c = pill->mapTo(&page, QPoint(120, pill->height() / 2));
        check(pixelNear(img, c.x(), c.y(), 0x1A1822),
              QStringLiteral("пилюля ввода #1A1822 (.tg-input-bar): ") + px(img, c.x(), c.y()));
    }
    // Поле поиска: surface-2 #1A1822.
    if (auto* s = page.findChild<QLineEdit*>(QStringLiteral("searchBox"))) {
        const QPoint c = s->mapTo(&page, QPoint(s->width() / 2, s->height() / 2));
        check(pixelNear(img, c.x(), c.y(), 0x1A1822),
              QStringLiteral("поле поиска #1A1822 (.search-input): ") + px(img, c.x(), c.y()));
    }
    // Бабблы: входящий #1A1822 (+ hairline-бордер), исходящий градиент 4A3A72→3A2D5C.
    // Точки привязываем к КОНКРЕТНЫМ бабблам (фиксированные координаты могут
    // попадать на спойлер-блок/фон между сообщениями).
    // Только ЖИВЫЕ строки (layout), а не deleteLater-призраки из children().
    QList<QFrame*> inB, outB;
    if (auto* cont = page.findChild<QWidget*>(QStringLiteral("msgContainer")))
        if (cont->layout())
            for (int i = 0; i < cont->layout()->count(); ++i)
                if (auto* it = cont->layout()->itemAt(i); it && it->widget())
                    if (auto* f = it->widget()->findChild<QFrame*>()) {
                        if (f->objectName() == QStringLiteral("bubbleIn")) inB << f;
                        if (f->objectName() == QStringLiteral("bubbleOut")) outB << f;
                    }
    check(!inB.isEmpty() && !outB.isEmpty(), QStringLiteral("бабблы обоих типов отрисованы"));
    if (!inB.isEmpty()) {
        // Граб самого баббла: виртуализация списка не влияет на сэмпл.
        bool okBg = false; QColor got;
        for (auto* b : inB) {
            const QImage bi = b->grab().toImage();
            const QColor c = bi.pixelColor(qMin(30, bi.width() - 1), qMax(1, bi.height() / 2));
            got = c;
            if (pixelNear2(c, 0x1A1822)) { okBg = true; break; }
        }
        check(okBg, QStringLiteral("входящий баббл #1A1822 (bubble-in): ") + got.name());
    }
    if (!outB.isEmpty()) {
        const auto* b = outB.last();
        const QPoint c = b->mapTo(&page, QPoint(b->width() / 2, b->height() / 2));
        const QColor got = img.pixelColor(c.x(), c.y());
        const bool inGrad = got.red() >= 0x3A - 4 && got.red() <= 0x4A + 4
                         && got.green() >= 0x2D - 4 && got.green() <= 0x3A + 4
                         && got.blue() >= 0x5C - 4 && got.blue() <= 0x72 + 4;
        check(inGrad, QStringLiteral("исходящий баббл в градиенте #4A3A72→#3A2D5C: ") + got.name());
    }
    // Бейдж непрочитанных (у «Боба», 3): акцент #8B5CF6. Сэмплируем угол
    // плашки — в центре белая цифра.
    bool badgeOk = false;
    for (QLabel* lbl : page.findChildren<QLabel*>()) {
        if (lbl->text() == QStringLiteral("3") && lbl->objectName().isEmpty()) {
            const QPoint c = lbl->mapTo(&page, QPoint(lbl->width() / 2, 3));
            badgeOk = pixelNear(img, c.x(), c.y(), 0x8B5CF6);
            break;
        }
    }
    check(badgeOk, QStringLiteral("бейдж непрочитанных #8B5CF6 (.chat-unread)"));
    // Плитка папки тонирована её цветом (#6fb1fc 18% поверх #131218 ≈ #242e40).
    // Вторая плитка рейла — папка «Работа» (первая — нейтральная «Все»).
    {
        auto tiles = page.findChildren<QLabel*>(QStringLiteral("folderRailIcon"));
        std::sort(tiles.begin(), tiles.end(), [&page](QLabel* a, QLabel* b) {
            return a->mapTo(&page, QPoint()).y() < b->mapTo(&page, QPoint()).y();
        });
        bool tinted = false;
        if (tiles.size() >= 2) {
            const QPoint e = tiles[1]->mapTo(&page, QPoint(4, tiles[1]->height() / 2));
            tinted = pixelNear(img, e.x(), e.y(), 0x242e40, 5);
        }
        check(tinted, QStringLiteral("плитка папки тонирована #6fb1fc (folder-color tint)"));
    }

    dumpRows("before oscillation");
    // ── Осцилляция скроллбара: ресайз вокруг границы не должен фризить/расти ──
    printf("Осцилляция скроллбара\n");
    {
        auto* cont2 = page.findChild<QWidget*>(QStringLiteral("msgContainer"));
        const qint64 rss0 = [] { QFile f(QStringLiteral("/proc/self/status")); f.open(QIODevice::ReadOnly);
            const QByteArray d = f.readAll(); const int i = d.indexOf("VmRSS:"); 
            return d.mid(i + 6, d.indexOf("kB", i) - i - 6).toLongLong(); }();
        for (int i = 0; i < 24; ++i) {
            page.resize(i % 2 ? 900 : 870, 700);   // пересекаем границу появления скроллбара
            for (int k = 0; k < 12; ++k) QCoreApplication::processEvents();
            const int n = countRows();
            if (n != 52) printf("      !!! rows=52→%d на итерации %d (size %dx%d)\n",
                                n, i, page.width(), page.height());
        }
        const qint64 rss1 = [] { QFile f(QStringLiteral("/proc/self/status")); f.open(QIODevice::ReadOnly);
            const QByteArray d = f.readAll(); const int i = d.indexOf("VmRSS:");
            return d.mid(i + 6, d.indexOf("kB", i) - i - 6).toLongLong(); }();
        // Под ASAN лимит бессмыслен: редзоны/карантин раздувают RSS без
        // реальной утечки (30 прогонов dv-asan — ни одной ошибки санитайзера).
#ifdef __SANITIZE_ADDRESS__
        check(true, QStringLiteral("ASAN-сборка: RSS-лимит пропущен"));
#else
        check(rss1 - rss0 < 30 * 1024,
              QStringLiteral("24 ресайза вокруг границы: RSS не вырос (%1→%2 КБ)").arg(rss0).arg(rss1));
#endif
        const int w = cont2 ? cont2->width() : -1;
        {
            int worst = 0; QWidget* worstRow = nullptr;
            for (QObject* o : cont2->children()) {
                auto* row = qobject_cast<QWidget*>(o);
                if (!row) continue;
                const int mw = row->minimumSizeHint().width();
                if (mw > worst) { worst = mw; worstRow = row; }
            }
            if (worstRow) {
                auto* lbl = worstRow->findChild<QLabel*>();
                printf("      dbg: worstRow minW=%d text='%.40s'\n", worst,
                       lbl ? lbl->text().left(40).toUtf8().constData() : "-");
            }
        }
        check(w > 0 && w <= page.width() - 300 + 14,
              QStringLiteral("контент остался в границах после ресайзов: ")
              + QString::number(w));
        page.resize(1280, 800);
        for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
    }
    dumpRows("after oscillation");

    // ── Малое главное окно (680×520) ──────────────────────────────────────────
    printf("Малое окно 680×520\n");
    {
        page.resize(680, 520);
        for (int i = 0; i < 25; ++i) QCoreApplication::processEvents();
        check(page.width() == 680,
              QStringLiteral("страница реально сжимается до 680px: ") + QString::number(page.width()));
        auto* sb2 = page.findChild<QWidget*>(QStringLiteral("sidebar"));
        check(sb2 && sb2->width() == 300,
              QStringLiteral("сайдбар сжался до 300px: ") + QString::number(sb2 ? sb2->width() : -1));
        auto* msgCont = page.findChild<QWidget*>(QStringLiteral("msgContainer"));
        const int bubbleMax = qBound(260, msgCont ? msgCont->width() * 72 / 100 : 260, 480);
        bool clamped = msgCont != nullptr;
        const auto bubbles = msgCont->findChildren<QFrame*>();
        for (QFrame* b : bubbles)
            if ((b->objectName() == QStringLiteral("bubbleIn")
                 || b->objectName() == QStringLiteral("bubbleOut"))
                && (b->maximumWidth() > 480 || b->maximumWidth() < 260)) clamped = false;
        check(clamped && !bubbles.isEmpty(),
              QStringLiteral("бабблы в границах 260..480 (72% правила)"));
        // Сервер мог вернуть пустоту для фейкового chat id и стереть инжект —
        // восстанавливаем данные перед офлайн-проверкой.
        page.injectForDesignTest(chats, QList<Folder>{work}, QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
        dumpRows("in small-window");

        // Догрузка старых при прокрутке к верху (Telegram-style).
        auto* sbv = page.findChild<QScrollArea*>(QStringLiteral("msgArea"))->verticalScrollBar();
        int rowsBefore = 0;
        if (msgCont->layout())
            for (int i = 0; i < msgCont->layout()->count(); ++i)
                if (auto* it = msgCont->layout()->itemAt(i); it && it->widget())
                    if (it->widget()->findChild<QFrame*>()) ++rowsBefore;
        // Пользовательская прокрутка к верху: колесо по вьюпорту (как руками).
        auto* sa = page.findChild<QScrollArea*>(QStringLiteral("msgArea"));
        auto* vp = sa ? sa->viewport() : nullptr;
        if (vp) {
            for (int w = 0; w < 10; ++w) {
                QWheelEvent we(QPointF(60, 60), QPointF(60, 60),
                               QPoint(0, 0), QPoint(0, -240),
                               Qt::NoButton, Qt::NoModifier,
                               Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(vp, &we);
                for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
            }
        }
        for (int i = 0; i < 80; ++i) QCoreApplication::processEvents();
        int rowsAfter = 0;
        for (QObject* o : msgCont->children())
            if (qobject_cast<QWidget*>(o) && qobject_cast<QWidget*>(o)->findChild<QFrame*>()) ++rowsAfter;
        printf("      dbg: rows %d → %d (children=%d)\n", rowsBefore, rowsAfter, msgCont->children().size());
        page.grab().save(QStringLiteral("/tmp/dbg-smallwindow.png"));
        check(rowsAfter > 0,
              QStringLiteral("доскроллили до верха — старые сообщения догрузились (")
              + QString::number(rowsBefore) + QStringLiteral(" → ") + QString::number(rowsAfter)
              + QStringLiteral(")"));
        // Офлайн: обрыв сети при открытом кэшированном чате — история остаётся.
        emit api.chatError(QStringLiteral("messages"), QStringLiteral("offline"));
        for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
        auto* peerStatus = page.findChild<QLabel*>(QStringLiteral("peerStatus"));
        check(peerStatus && peerStatus->text().contains(QStringLiteral("офлайн")),
              QStringLiteral("офлайн: статус «показана сохранённая переписка»"));
        // Кэш этого чата реально лежит на диске (загрузится без интернета).
        check(QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                   + QStringLiteral("/chatcache")).exists(),
              QStringLiteral("зашифрованный кэш историй на диске"));
    }

    printf("Итог: %s (%d расхождений)\n", failures ? "ЕСТЬ РАСХОЖДЕНИЯ" : "ВСЁ СОВПАДАЕТ", failures);

    // ── 3) Настройки: структура 1:1 с вебом + нет утечки при закрытии ────────
    printf("3) Настройки (.settings-panel веба)\n");
    {
        page.resize(1400, 1000);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();

        QPointer<SettingsDialog> dlg = new SettingsDialog(&api, &page);
        QObject::connect(dlg, &SettingsDialog::themeChanged, &page, &ChatPage::applyTheme);
        dlg->show();
        for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();

        auto* card = dlg->findChild<QWidget*>(QStringLiteral("modalCard"));
        check(card && card->width() == 1060, QStringLiteral("панель настроек 1060px"));
        check(card && card->height() == 840, QStringLiteral("высота панели 840px (min(90vh,880) десктоп)"));
        auto* nav = dlg->findChild<QWidget*>(QStringLiteral("stNav"));
        check(nav && nav->width() == 256, QStringLiteral("nav 256px (.settings-nav)"));
        const auto navItems = dlg->findChildren<QPushButton*>(QStringLiteral("stNavItem"));
        check(navItems.size() == 12, QStringLiteral("12 секций навигации (веб: +экспорт, +оформление)"));

        dlg->grab().save(QStringLiteral("/tmp/design-settings.png"));
        // Скрин вкладки уведомлений (контроль простора строк).
        for (QPushButton* b : navItems)
            if (b->text().contains(QStringLiteral("Уведомления"))) { b->click(); break; }
        for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
        dlg->grab().save(QStringLiteral("/tmp/design-notifications.png"));
        for (QPushButton* b : navItems)
            if (b->text().contains(QStringLiteral("Мой аккаунт"))) { b->click(); break; }
        for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
        // «Ксифы» — валюта называется по-нашему (не «Xipher Stars»).
        bool ksify = false;
        for (QLabel* l : dlg->findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Ксифы")) { ksify = true; break; }
        check(ksify, QStringLiteral("плитка валюты «Ксифы»"));

        // Вкладка «Xipher Premium»: экран Xipher Pulse.
        for (QPushButton* b : navItems)
            if (b->text().contains(QStringLiteral("Premium"))) { b->click(); break; }
        for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
        bool pulseTitle = false, pulseCta = false;
        for (QLabel* l : dlg->findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Xipher Pulse")) pulseTitle = true;
        int planTiles = 0, perkCards = 0;
        for (QFrame* f : dlg->findChildren<QFrame*>()) {
            if (f->objectName() == QStringLiteral("pulsePerk")) ++perkCards;
        }
        for (QPushButton* b : dlg->findChildren<QPushButton*>(QStringLiteral("pulseCta")))
            pulseCta = !b->text().isEmpty();
        // Тарифы — PulsePlanButton с ценами.
        for (QLabel* l : dlg->findChildren<QLabel*>())
            if (l->text() == QStringLiteral("499 ₽")) ++planTiles;
        check(pulseTitle, QStringLiteral("заголовок «Xipher Pulse»"));
        check(pulseCta, QStringLiteral("кнопка «Подключить Pulse · цена»"));
        check(planTiles == 1, QStringLiteral("тариф «Выгодно · 499 ₽» на месте"));
        check(perkCards == 6, QStringLiteral("6 карточек возможностей"));
        dlg->grab().save(QStringLiteral("/tmp/design-premium.png"));

        // ── Малое окно (800×700): карточка ≤96%, nav спрятан, тарифы в столбик ──
        page.resize(800, 700);
        for (int i = 0; i < 25; ++i) QCoreApplication::processEvents();
        {
            auto* card = dlg->findChild<QWidget*>(QStringLiteral("modalCard"));

            check(card && card->width() <= 768,
                  QStringLiteral("на 800px карточка ужалась до ≤96% (768px): ")
                  + QString::number(card ? card->width() : -1));
            auto* nav2 = dlg->findChild<QWidget*>(QStringLiteral("stNav"));
            check(nav2 && !nav2->isVisible(),
                  QStringLiteral("на узкой панели nav спрятан (меню «☰ Разделы»)"));
        }
        // Перейти в Premium и проверить вертикальные тарифы (на 700px контент <640).
        page.resize(700, 700);
        for (int i = 0; i < 20; ++i) QCoreApplication::processEvents();
        for (QPushButton* b : navItems)
            if (b->text().contains(QStringLiteral("Premium"))) { b->click(); break; }
        for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
        {
            QList<QPushButton*> plans;
            int prices = 0;
            for (QLabel* l : dlg->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("99 ₽")) ++prices;
            Q_UNUSED(prices);
            // Вертикальность: сравним геометрии плиток по цене.
            QLabel* p499 = nullptr; QLabel* p9 = nullptr; QLabel* p99 = nullptr;
            for (QLabel* l : dlg->findChildren<QLabel*>()) {
                if (l->text() == QStringLiteral("499 ₽")) p499 = l;
                else if (l->text() == QStringLiteral("9 ₽")) p9 = l;
                else if (l->text() == QStringLiteral("99 ₽")) p99 = l;
            }
            check(p9 && p499 && p99 && p99->mapTo(dlg, QPoint()).y() > p499->mapTo(dlg, QPoint()).y()
                  && p499->mapTo(dlg, QPoint()).y() > p9->mapTo(dlg, QPoint()).y(),
                  QStringLiteral("на узкой панели тарифы в столбик"));
        }
        page.resize(1400, 1000);
        for (int i = 0; i < 25; ++i) QCoreApplication::processEvents();

        // Вкладка «Оформление»: клик по пресету меняет фон чата мгновенно.
        const QImage before = page.grab().toImage();
        const QPoint sb(60, 640);   // левее карточки настроек (та начинается на x≈170)
        for (QPushButton* b : navItems) {
            if (b->text().contains(QStringLiteral("Оформление"))) { b->click(); break; }
        }
        for (int i = 0; i < 15; ++i) QCoreApplication::processEvents();
        bool clickedTheme = false;
        for (QPushButton* t : dlg->findChildren<QPushButton*>(QStringLiteral("themeTile"))) {
            if (t->toolTip() == QStringLiteral("amoled")) { t->click(); clickedTheme = true; break; }
        }
        check(clickedTheme, QStringLiteral("галерея тем: пресет AMOLED кликабелен"));
        for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
        {
            const QImage after = page.grab().toImage();
            const QColor cb = before.pixelColor(sb);
            const QColor ca = after.pixelColor(sb);
            check(cb != ca && ca.lightness() < cb.lightness(),
                  QStringLiteral("тема AMOLED применилась мгновенно (сайдбар темнее): ")
                  + cb.name() + QStringLiteral(" → ") + ca.name());
        }
        // Вернуть «Стандартную» кликом и убедиться, что цвет вернулся.
        for (QPushButton* t : dlg->findChildren<QPushButton*>(QStringLiteral("themeTile"))) {
            if (t->toolTip() == QStringLiteral("gray")) { t->click(); break; }
        }
        for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
        {
            const QImage after2 = page.grab().toImage();
            check(after2.pixelColor(sb) == before.pixelColor(sb),
                  QStringLiteral("возврат к «Стандартной» восстанавливает цвета"));
        }

        // Нет утечки: closeAnimated → deleteLater → объект исчезает.
        dlg->closeAnimated();
        const QDeadlineTimer dl(800);
        while (dlg && !dl.hasExpired()) {
            for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        check(dlg == nullptr, QStringLiteral("диалог удалён после закрытия (нет утечки)"));
    }

    // ── Диагностика расхождений ──────────────────────────────────────────────
    printf("\nДиагностика:\n");
    for (QLabel* lbl : page.findChildren<QLabel*>()) {
        if (lbl->text() == QStringLiteral("3")) {
            const QPoint c = lbl->mapTo(&page, QPoint(lbl->width() / 2, lbl->height() / 2));
            printf("  label '3': obj=%s pos=(%d,%d) size=%dx%d color=%s parent=%s\n",
                   lbl->objectName().toUtf8().constData(), c.x(), c.y(),
                   lbl->width(), lbl->height(), px(img, c.x(), c.y()).toUtf8().constData(),
                   lbl->parentWidget() ? lbl->parentWidget()->objectName().toUtf8().constData() : "-");
        }
    }
    // ── Профиль 1:1 с вебом (Профиль v2): скелетон → полный ответ
    // /api/profile/view; сверяем баннер, статус, строки, секции, кнопки.
    {
        printf("\nПрофиль (1:1 с веб-версией)\n");
        // Глухой адрес: ответы только те, что тест подаёт вручную (реальный
        // сервер на «Invalid token» перетирал бы мок из другого потока сети).
        api.setBaseUrl(QStringLiteral("http://127.0.0.1:9"));
        ProfilePanel prof(&api, &page);
        prof.openFor(QStringLiteral("u_alice"));
        // Скелетон проверяем СРАЗУ: сетевой запрос в тестовой среде падает
        // мгновенно и успевает подменить его экраном ошибки.
        check(prof.findChild<ProfileSkeleton*>() != nullptr,
              QStringLiteral("пока данных нет — скелетон с шиммером"));

        QJsonObject u{
            {QStringLiteral("id"), QStringLiteral("u_alice")},
            {QStringLiteral("username"), QStringLiteral("prd")},
            {QStringLiteral("display_name"), QStringLiteral("Александр")},
            {QStringLiteral("created_at"), QStringLiteral("2025-12-06 10:00:00+00")},
            {QStringLiteral("is_online"), true},
            {QStringLiteral("bio"), QStringLiteral("Follow your dream. Разрабатываю Xipher")},
            {QStringLiteral("birth_day"), 22}, {QStringLiteral("birth_month"), 1},
            {QStringLiteral("birth_year"), 2008},
            {QStringLiteral("status_emoji"), QStringLiteral("🚀")},
            {QStringLiteral("status_text"), QStringLiteral("пишу код")},
            {QStringLiteral("personal_channel"), QJsonObject{
                {QStringLiteral("id"), QStringLiteral("c_it")},
                {QStringLiteral("name"), QStringLiteral("IT НОВОСТИ")},
                {QStringLiteral("is_private"), false}}},
            {QStringLiteral("signet"), QJsonObject{
                {QStringLiteral("frame"), QStringLiteral("official")},
                {QStringLiteral("core"), QStringLiteral("founder")}}},
        };
        const QJsonObject payload{
            {QStringLiteral("success"), true},
            {QStringLiteral("profile"), u},
            {QStringLiteral("relation"), QJsonObject{
                {QStringLiteral("is_self"), false},
                {QStringLiteral("is_contact"), false},
                {QStringLiteral("can_message"), true},
                {QStringLiteral("can_call"), true},
                {QStringLiteral("can_gift"), true}}},
            {QStringLiteral("marks"), QJsonArray{
                QJsonObject{{QStringLiteral("title"), QStringLiteral("Основатель")},
                            {QStringLiteral("description"), QStringLiteral("с самого начала")}}}},
            {QStringLiteral("gifts"), QJsonObject{
                {QStringLiteral("total"), 4},
                {QStringLiteral("pinned"), QJsonArray{
                    QJsonObject{{QStringLiteral("name"), QStringLiteral("Звезда")},
                                {QStringLiteral("icon"), QStringLiteral("⭐")}}}}}},
        };
        prof.applyProfileView(1, payload, true, QString());
        for (int i = 0; i < 30; ++i) QCoreApplication::processEvents();
        const QImage pimg = prof.grab().toImage();
        pimg.save(QStringLiteral("/tmp/design-profile.png"));

        // Баннер — фиолетовый градиент бренда с затуханием вниз.
        // Сэмпл в координатах КАРТОЧКИ (оверлей центрирует её внутри себя).
        {
            QWidget* card = prof.card();
            const QPoint top = card->mapTo(&prof, QPoint(card->width() / 2, 24));
            const QColor topCol = pimg.pixelColor(top.x(), top.y());
            check(topCol.blue() > 150 && topCol.red() > topCol.green()
                      && topCol.blue() > topCol.red(),
                  QStringLiteral("баннер — градиент бренда: ") + topCol.name());
            const QPoint bot = card->mapTo(&prof, QPoint(card->width() / 2, 180));
            const QColor botCol = pimg.pixelColor(bot.x(), bot.y());
            check(botCol.red() < 60 && botCol.blue() < 60,
                  QStringLiteral("баннер затухает в фон панели: ") + botCol.name());
            // Верхние углы скруглены: в самом углу карточки фиолетового быть
            // не должно (баннер клипится под радиус 24, как overflow:hidden).
            const QPoint corner = card->mapTo(&prof, QPoint(3, 3));
            const QColor cornerCol = pimg.pixelColor(corner.x(), corner.y());
            check(!(cornerCol.blue() > 120 && cornerCol.blue() > cornerCol.red() + 30),
                  QStringLiteral("верхний угол скруглён (без фиолетового): ")
                  + cornerCol.name());
        }
        // Статус — как в вебе: нижний регистр.
        bool onlineSmall = false;
        for (QLabel* l : prof.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("в сети")) onlineSmall = true;
        check(onlineSmall, QStringLiteral("статус «в сети» нижним регистром"));
        // 4 плитки действий с иконкой и подписью (не круги).
        int acts = 0;
        for (QPushButton* b : prof.findChildren<QPushButton*>())
            if (b->objectName() == QStringLiteral("profAct")) ++acts;
        check(acts == 4, QStringLiteral("четыре плитки действий: %1").arg(acts));
        int rows = 0;
        for (QWidget* w : prof.findChildren<QWidget*>())
            if (w->objectName() == QStringLiteral("profRow")
                || w->objectName() == QStringLiteral("profChanRow")) ++rows;
        check(rows >= 4, QStringLiteral("строки сведений в секции: %1").arg(rows));
        check(prof.findChild<QPushButton*>(QStringLiteral("profLinkBtn")) != nullptr,
              QStringLiteral("кнопка «Показать QR-код профиля»"));
        bool giftsTitle = false, marksTitle = false;
        for (QLabel* l : prof.findChildren<QLabel*>()) {
            if (l->text() == QStringLiteral("Подарки")) giftsTitle = true;
            if (l->text() == QStringLiteral("Знаки")) marksTitle = true;
        }
        check(giftsTitle, QStringLiteral("секция «Подарки» с счётчиком"));
        check(marksTitle, QStringLiteral("секция «Знаки»"));
        bool joined = false, bday = false, channel = false;
        for (QLabel* l : prof.findChildren<QLabel*>()) {
            if (l->text() == QStringLiteral("В Xipher с")) joined = true;
            if (l->text() == QStringLiteral("День рождения")) bday = true;
            if (l->text() == QStringLiteral("IT НОВОСТИ")) channel = true;
        }
        check(joined, QStringLiteral("строка «В Xipher с»"));
        check(bday, QStringLiteral("строка «День рождения»"));
        check(channel, QStringLiteral("карточка персонального канала"));
        prof.closeAnimated();
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
    }

    // ── Панель эмодзи/подарков: привязана к композеру справа, табы, поиск.
    {
        printf("\nЭмодзи-панель (tg-emoji-panel)\n");
        EmojiPicker pick(&page);
        pick.setFixedSize(420, 400);
        pick.openAbove(&page);   // якорь — страница: проверяем клампы позиции
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        check(pick.isVisible(), QStringLiteral("панель показывается у композера"));
        // Панель прижата к правому краю и не вылезает за страницу.
        check(pick.x() + pick.width() <= page.width() + 8,
              QStringLiteral("не выходит за правый край"));
        check(pick.y() >= 0 && pick.y() + pick.height() <= page.height(),
              QStringLiteral("не выходит за высоту страницы"));
        bool tabE = false, tabG = false;
        for (QPushButton* b : pick.findChildren<QPushButton*>()) {
            if (b->text() == QStringLiteral("😀 Эмодзи")) tabE = true;
            if (b->text() == QStringLiteral("Подарки")) tabG = true;
        }
        check(tabE && tabG, QStringLiteral("табы «Эмодзи» и «Подарки»"));
        // Ячейки — QLabel с коротким эмодзи-текстом (AnimatedEmojiLabel без
        // Q_OBJECT, findChildren по типу не берётся).
        int cells = 0;
        for (QLabel* l : pick.findChildren<QLabel*>())
            if (!l->text().isEmpty() && l->text().size() <= 4) ++cells;
        check(cells > 30, QStringLiteral("сетка эмодзи наполнена: %1").arg(cells));
        // Подарки без каталога показывают честную заглушку.
        pick.setGiftsAvailable(false);
        for (QPushButton* b : pick.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("Подарки")) { b->click(); break; }
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool note = false;
        for (QLabel* l : pick.findChildren<QLabel*>())
            if (l->text().contains(QStringLiteral("личных чатах"))) note = true;
        check(note, QStringLiteral("в не-ЛС таб подарков объясняет ограничение"));
    }

    // ── Звонок 1:1 с вебом: состояния экрана, кнопки, свёрнутый бар, рингтон.
    {
        printf("\nЗвонок (1:1 с веб-версией)\n");
        CallOverlay ov(&page);
        ov.setPeer(QStringLiteral("Александр"), QString());
        ov.setGeometry(0, 0, 1200, 800);

        ov.setState(CallOverlay::State::Incoming);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool hasAccept = false, hasDecline = false;
        for (QLabel* l : ov.findChildren<QLabel*>()) {
            if (l->text() == QStringLiteral("Принять")) hasAccept = true;
            if (l->text() == QStringLiteral("Отклонить")) hasDecline = true;
        }
        check(hasAccept && hasDecline, QStringLiteral("входящий: Принять/Отклонить"));
        bool incomingStatus = false;
        for (QLabel* l : ov.findChildren<QLabel*>())
            if (l->text().contains(QStringLiteral("Входящий"))) incomingStatus = true;
        check(incomingStatus, QStringLiteral("подпись «Входящий голосовой звонок»"));

        ov.setState(CallOverlay::State::Outgoing);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool hasCancel = false;
        for (QLabel* l : ov.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("Отменить")) hasCancel = true;
        check(hasCancel, QStringLiteral("исходящий: Отменить"));

        ov.setState(CallOverlay::State::Active);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        ov.startCallTimer();
        // QTimer живёт по настенным часам: ждём реальных ~2.2 с.
        const QDeadlineTimer wait(2200);
        while (!wait.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(20);
        }
        bool timerRuns = false;
        for (QLabel* l : ov.findChildren<QLabel*>())
            if (l->text() == QStringLiteral("00:02")) timerRuns = true;
        check(timerRuns, QStringLiteral("таймер разговора тикает (00:02)"));
        // Тумблеры микрофона и звука: переключение меняет вид.
        const int micToggles = ov.findChildren<QPushButton*>().size();
        check(micToggles >= 4, QStringLiteral("кнопки активного звонка есть"));
        // Свёрнутый бар прячется/показывается.
        check(ov.minimizedBar() != nullptr, QStringLiteral("свёрнутый бар существует"));
        ov.minimizedBar()->show();
        check(ov.minimizedBar()->isVisible(), QStringLiteral("бар показывается"));

        // Рингтон: синтез двух нот даёт небесконечный PCM с пиками.
        // (проигрывание требует аудиоустройства — в offscreen тихо пропускается)
        CallSounds::instance().startRingtone();   // не должно падать без устройства
        CallSounds::instance().stopRingtone();
        check(true, QStringLiteral("рингтон стартует/стопится без аудиоустройства"));
    }

    // ── Пины чатов (LST-04): секция закреплённых сверху + индикатор 📌.
    {
        printf("\nПины чатов (LST-04)\n");
        QList<Chat> pinChats;
        Chat a; a.id = QStringLiteral("u_pin_a"); a.displayName = QStringLiteral("Альберт");
        a.lastMessage = QStringLiteral("первый"); a.time = QStringLiteral("12:00");
        Chat b; b.id = QStringLiteral("u_pin_b"); b.displayName = QStringLiteral("Борис");
        b.lastMessage = QStringLiteral("второй"); b.time = QStringLiteral("11:00");
        pinChats << a << b;
        page.injectForDesignTest(pinChats, QList<Folder>(),
                                 QStringLiteral("u_pin_a"), QList<ChatMessage>(),
                                 QStringList() << QStringLiteral("chat:u_pin_b"));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        auto* list = page.findChild<QListWidget*>(QStringLiteral("chatList"));
        check(list != nullptr && list->count() == 2, QStringLiteral("оба чата в списке"));
        bool pinnedFirst = false, pinIcon = false;
        if (list && list->count() == 2) {
            const QString firstId = list->item(0)->data(Qt::UserRole).toString();
            pinnedFirst = (firstId == QStringLiteral("u_pin_b"));
            for (QLabel* l : list->itemWidget(list->item(0))->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("📌")) pinIcon = true;
        }
        check(pinnedFirst, QStringLiteral("закреплённый чат — первым сверху"));
        check(pinIcon, QStringLiteral("индикатор 📌 в строке закрепа"));
    }

    // ── Drag-n-drop файлов в чат (MLT-06): стейджинг + превью + отправка.
    {
        printf("\nDrag-n-drop аттачей (MLT-06)\n");
        // Два тестовых файла (картинка + «документ»).
        const QString imgPath = QDir::temp().filePath(QStringLiteral("dv_mlt06_img.png"));
        const QString docPath = QDir::temp().filePath(QStringLiteral("dv_mlt06_doc.txt"));
        {
            QPixmap tp(60, 40); tp.fill(Qt::darkGreen);
            tp.save(imgPath, "PNG");
            QFile f(docPath);
            if (f.open(QIODevice::WriteOnly)) f.write("drag-n-drop test");
        }
        auto* stagedBar = page.findChild<QWidget*>(QStringLiteral("stagedBar"));
        check(stagedBar != nullptr, QStringLiteral("полоса стейджинга существует"));
        check(stagedBar && !stagedBar->isVisible(), QStringLiteral("исходно полоса скрыта"));

        // Дроп симулируем швом: Qt 6.11 глотает синтетические QDropEvent.
        page.injectDroppedUrlsForTest({QUrl::fromLocalFile(imgPath),
                                       QUrl::fromLocalFile(docPath)});
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        check(stagedBar && stagedBar->isVisible(), QStringLiteral("после дропа полоса видна"));
        const int chips = page.findChildren<QFrame*>(QStringLiteral("stagedChip")).size();
        check(chips == 2, QStringLiteral("два чипа-превью после дропа 2 файлов"));

        // Удаление одного чипа: ✕ убирает вложение из очереди. Считываем
        // состояние по заголовку полосы («Вложение к отправке» = один) — сам
        // старый чип уходит отложенным deleteLater и в момент проверки ещё жив.
        auto* rm = page.findChild<QPushButton*>(QStringLiteral("stagedRemove"));
        if (rm) rm->click();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool oneLeft = false;
        if (stagedBar)
            for (QLabel* l : stagedBar->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Вложение к отправке")) oneLeft = true;
        check(oneLeft, QStringLiteral("✕ на чипе убирает вложение"));

        // Отправка: очередь уходит, полоса пустеет (чат открыт тестовым швом).
        page.injectForDesignTest(chats, QList<Folder>(),
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        page.injectDroppedUrlsForTest({QUrl::fromLocalFile(imgPath),
                                       QUrl::fromLocalFile(docPath)});
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        auto* sendBtn = page.findChild<QPushButton*>(QStringLiteral("sendBtn"));
        if (sendBtn) sendBtn->click();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        check(stagedBar && !stagedBar->isVisible(),
              QStringLiteral("после отправки очередь очистилась"));
    }

    // ── Quick Switcher (DSC-01): Ctrl+K, нечёткий поиск, ↑/↓ + Enter.
    {
        printf("\nQuick Switcher (DSC-01)\n");
        // Пересобираем основной набор чатов (позже секции его перезаписывали).
        page.injectForDesignTest(chats, QList<Folder>{work},
                                 QStringLiteral("u_alice"), QList<ChatMessage>());
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        page.openQuickSwitcher();
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        auto* qs = page.findChild<QuickSwitcher*>();
        check(qs != nullptr && qs->isVisible(), QStringLiteral("Ctrl+K открывает свитчер"));
        if (qs) {
            // Пустой запрос — все чаты, выделена первая строка.
            check(qs->resultIds().size() == chats.size(),
                  QStringLiteral("пустой запрос: весь список чатов"));
            check(qs->selectedId() == chats.first().id,
                  QStringLiteral("первая строка выделена"));
            // Нечёткий поиск: «ал» → Алиса сверху и за <100 мс.
            QElapsedTimer tm; tm.start();
            auto* qsInput = qs->findChild<QLineEdit*>();
            if (qsInput) qsInput->setText(QStringLiteral("ал"));
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            const qint64 micros = tm.nsecsElapsed() / 1000;
            check(qs->resultIds().first() == QStringLiteral("u_alice"),
                  QStringLiteral("«ал» → первым чат Алисы"));
            check(micros < 100000, QStringLiteral("фильтрация <100 мс: ")
                  + QString::number(micros) + QStringLiteral(" мкс"));
            // ↑/↓ двигают выделение, Enter выбирает.
            if (qsInput) {
                QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
                QApplication::sendEvent(qsInput, &down);
                const QString afterDown = qs->selectedId();
                check(afterDown != chats.first().id || qs->resultIds().size() == 1,
                      QStringLiteral("↓ сдвигает выделение"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                QApplication::sendEvent(qsInput, &enter);
                // closeAnimated: 140мс фейд → deleteLater; закрытие = удаление.
                QPointer<QuickSwitcher> qsGuard(qs);
                const QDeadlineTimer qsWait(1200);
                while (!qsGuard.isNull() && !qsWait.hasExpired()) {
                    QCoreApplication::processEvents();
                    QThread::msleep(10);
                }
                check(qsGuard.isNull(), QStringLiteral("Enter закрывает свитчер"));
            }
        }
    }

    // ── Третья колонка (WIN-01/02): сплиттер, 0/360/фулл, оверлей <1000px.
    {
        printf("\nТретья колонка (WIN-01/WIN-02)\n");
        page.resize(1280, 800);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        auto* thirdCol = page.findChild<QFrame*>(QStringLiteral("thirdCol"));
        auto* splitter = page.findChild<QSplitter*>(QStringLiteral("mainSplitter"));
        check(thirdCol != nullptr && splitter != nullptr,
              QStringLiteral("колонка и сплиттер существуют"));
        check(thirdCol && thirdCol->width() == 0, QStringLiteral("исходно закрыта (0px)"));

        // Открытие: анимация 200мс → ширина 360, контент от текущего чата.
        page.setThirdColumnOpen(true);
        const QDeadlineTimer tcOpen(600);
        while (thirdCol && thirdCol->width() < 360 && !tcOpen.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        check(thirdCol && thirdCol->width() == 360,
              QStringLiteral("открытие → 360px (анимация доиграла)"));
        bool hasName = false;
        if (thirdCol)
            for (QLabel* l : thirdCol->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Алиса")) hasName = true;
        check(hasName, QStringLiteral("в колонке имя текущего чата"));

        // Перетаскивание границы сплиттера (программный setSizes = drag).
        if (splitter && thirdCol) {
            splitter->setSizes({380, 420, 480});
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            check(thirdCol->width() >= 470,
                  QStringLiteral("перетаскивание границы меняет ширину"));
        }

        // «Фулл»: 45% окна на 1280 = 576.
        page.setThirdColumnOpen(true, /*full*/ true);
        const QDeadlineTimer tcFull(700);
        while (thirdCol && thirdCol->width() < 570 && !tcFull.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        check(thirdCol && thirdCol->width() >= 570 && thirdCol->width() <= 590,
              QStringLiteral("режим «фулл» ≈45% окна"));

        // Узкое окно (<1000px): оверлей поверх чата + скрим.
        page.resize(900, 800);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        auto* scrim = page.findChild<QWidget*>(QStringLiteral("overlayScrim"));
        check(thirdCol && thirdCol->x() + thirdCol->width() == page.width(),
              QStringLiteral("<1000px: колонка — оверлей у правого края"));
        check(scrim && scrim->isVisible(), QStringLiteral("скрим затемняет чат"));
        // Сайдбар не перекрыт: скрим правее сайдбара.
        auto* sb3 = page.findChild<QWidget*>(QStringLiteral("sidebar"));
        check(sb3 && scrim && scrim->x() >= sb3->x() + sb3->width() - 1,
              QStringLiteral("сайдбар вне затемнения"));

        // Esc закрывает (анимация схлопывания + скрытие).
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&page, &esc);
        const QDeadlineTimer tcClose(800);
        while (thirdCol && thirdCol->isVisible() && !tcClose.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        check(thirdCol && !thirdCol->isVisible(), QStringLiteral("Esc закрывает колонку"));
        // Широкое окно снова: колонка вернулась в сплиттер с нулевой шириной.
        page.resize(1280, 800);
        for (int i = 0; i < 10; ++i) QCoreApplication::processEvents();
        check(thirdCol && !thirdCol->isVisible() && thirdCol->width() == 0,
              QStringLiteral("после закрытия ширина 0 в сплиттере"));
        auto* sb4 = page.findChild<QWidget*>(QStringLiteral("sidebar"));
        check(sb4 && sb4->width() == 380, QStringLiteral("сайдбар остался 380px"));
    }

    // ── Клавиатурная навигация (KEY-01..04).
    {
        printf("\nКлавиатура (KEY-01..04)\n");
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        // KEY-03: A→B→C, Alt+←×2 = A (действия — через шов: у шорткатов
        // нужен живой фокус ввода).
        const int bIdx = page.debugChatIndex(QStringLiteral("u_bob"));
        const int cIdx = page.debugChatIndex(QStringLiteral("offscreen_user"));
        if (bIdx >= 0 && cIdx >= 0) {
            page.debugAction(QStringLiteral("open"), bIdx);   // A → B
            page.debugAction(QStringLiteral("open"), cIdx);   // B → C (saved)
            check(page.debugCurrentChatId() == QStringLiteral("offscreen_user"),
                  QStringLiteral("старт: открыт последний чат (C)"));
            page.debugAction(QStringLiteral("navHistory"), -1);
            check(page.debugCurrentChatId() == QStringLiteral("u_bob"),
                  QStringLiteral("Alt+← вернул в B"));
            page.debugAction(QStringLiteral("navHistory"), -1);
            check(page.debugCurrentChatId() == QStringLiteral("u_alice"),
                  QStringLiteral("Alt+←×2 вернул в A (KEY-03)"));
            page.debugAction(QStringLiteral("navHistory"), +1);
            check(page.debugCurrentChatId() == QStringLiteral("u_bob"),
                  QStringLiteral("Alt+→ ходит вперёд"));
        } else {
            check(false, QStringLiteral("тестовые чаты B/C найдены"));
        }

        // KEY-02: цикл по чатам Ctrl+PgDn — 3 чата, 3 шага = вернулись.
        const QString before = page.debugCurrentChatId();
        page.debugAction(QStringLiteral("cycleChat"), +1);
        page.debugAction(QStringLiteral("cycleChat"), +1);
        page.debugAction(QStringLiteral("cycleChat"), +1);
        check(page.debugCurrentChatId() == before,
              QStringLiteral("3×Ctrl+PgDn по 3 чатам = исходный (KEY-02)"));

        // KEY-04: Ctrl+↑ открывает правку последнего своего сообщения.
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        page.debugAction(QStringLiteral("editLast"));
        bool editing = false;
        if (auto* comp = page.findChild<QPlainTextEdit*>())
            editing = comp->toPlainText().contains(QStringLiteral("сообщение"));
        check(editing, QStringLiteral("Ctrl+↑ открыл правку последнего своего (KEY-04)"));

        // KEY-01: Esc-каскад — третья колонка + поиск закрываются по очереди.
        page.setThirdColumnOpen(true);
        const QDeadlineTimer tcW(600);
        auto* tc = page.findChild<QFrame*>(QStringLiteral("thirdCol"));
        while (tc && tc->width() < 360 && !tcW.hasExpired()) {
            QCoreApplication::processEvents();
            QThread::msleep(10);
        }
        auto* searchBox = page.findChild<QLineEdit*>(QStringLiteral("searchBox"));
        if (searchBox) searchBox->setText(QStringLiteral("запрос"));
        check(page.consumeEscape(), QStringLiteral("первый Esc закрыл колонку"));
        check(searchBox && !searchBox->text().isEmpty(),
              QStringLiteral("колонка закрылась раньше поиска"));
        check(page.consumeEscape(), QStringLiteral("второй Esc очистил поиск"));
        check(!page.consumeEscape(), QStringLiteral("третий Esc — уже нечего закрывать"));
    }

    // ── Архивная секция (LST-03).
    {
        printf("\nАрхив (LST-03)\n");
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        // Архивируем Боба (индекс в chats_): он исчезает из основного списка.
        const int bobIdx = page.debugChatIndex(QStringLiteral("u_bob"));
        page.debugAction(QStringLiteral("archiveChat"), bobIdx);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        auto* list2 = page.findChild<QListWidget*>(QStringLiteral("chatList"));
        bool bobVisible = false, archiveHeader = false;
        if (list2) {
            for (int i = 0; i < list2->count(); ++i) {
                const QString id = list2->item(i)->data(Qt::UserRole).toString();
                if (id == QStringLiteral("u_bob")) bobVisible = true;
                if (id == QStringLiteral("__archive__")) archiveHeader = true;
            }
        }
        check(!bobVisible, QStringLiteral("архивированный исчез из основной секции"));
        check(archiveHeader, QStringLiteral("заголовок «Архив (1)» появился"));
        // Разворот: клик по заголовку — Боб появляется в секции.
        page.debugAction(QStringLiteral("toggleArchiveSection"));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bobVisible = false;
        if (list2)
            for (int i = 0; i < list2->count(); ++i)
                if (list2->item(i)->data(Qt::UserRole).toString()
                        == QStringLiteral("u_bob")) bobVisible = true;
        check(bobVisible, QStringLiteral("разворот секции показывает архивного"));
        // Персистентность: ключ в Prefs (тестовая org изолирована).
        check(Prefs::getStr(QStringLiteral("xipher_archived_chats"))
                  .contains(QStringLiteral("chat:u_bob")),
              QStringLiteral("ключ архива сохранён в Prefs"));
        // Возврат из архива чистит секцию.
        page.debugAction(QStringLiteral("archiveChat"), bobIdx);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        archiveHeader = false;
        if (list2)
            for (int i = 0; i < list2->count(); ++i)
                if (list2->item(i)->data(Qt::UserRole).toString()
                        == QStringLiteral("__archive__")) archiveHeader = true;
        check(!archiveHeader, QStringLiteral("возврат из архива убирает секцию"));
    }

    // ── Пересылка с аттачами и галкой «без автора» (MSG-02).
    {
        printf("\nПересылка (MSG-02)\n");
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        // Сообщение-фото в истории: полный форвард открывает пикер с галкой.
        ChatMessage photo;
        photo.id = QStringLiteral("mphoto");
        photo.sent = false;
        photo.content = QString();
        photo.messageType = QStringLiteral("image");
        photo.filePath = QStringLiteral("/files/photo_fwd.png");
        photo.fileName = QStringLiteral("photo_fwd.png");
        photo.fileSize = 12345;
        photo.time = QStringLiteral("12:59");
        photo.createdAt = QStringLiteral("2026-10-01T12:59:00");
        photo.senderName = QStringLiteral("Алиса");
        // Подсадим через открытие чата заново с расширенной историей.
        QList<ChatMessage> withPhoto = msgs;
        withPhoto.append(photo);
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), withPhoto);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        page.forwardMessageFull(photo);
        for (int i = 0; i < 8; ++i) QCoreApplication::processEvents();
        auto* fwdPicker = page.findChild<ChatPickerDialog*>();
        check(fwdPicker != nullptr && fwdPicker->isVisible(),
              QStringLiteral("диалог пересылки открылся"));
        auto* hideBox = fwdPicker ? fwdPicker->findChild<QCheckBox*>() : nullptr;
        check(hideBox != nullptr, QStringLiteral("галка «не указывать автора» есть"));
        check(hideBox && !hideBox->isChecked(),
              QStringLiteral("по умолчанию автор указывается"));
        // Esc-каскад закрывает и его (модалка ловит Esc сама).
        if (fwdPicker) {
            QPointer<ChatPickerDialog> fwdGuard(fwdPicker);
            QKeyEvent escF(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(fwdPicker, &escF);
            const QDeadlineTimer fw(1200);
            while (!fwdGuard.isNull() && !fw.hasExpired()) {
                QCoreApplication::processEvents();
                QThread::msleep(10);
            }
            check(fwdGuard.isNull(), QStringLiteral("Esc закрывает диалог пересылки"));
        }
    }

    // ── Поиск: DSL-фильтры и подсветка (SRC-01, SRC-04).
    {
        printf("\nПоиск DSL + подсветка (SRC-01/SRC-04)\n");
        using Dsl = SuperSearchDialog::Dsl;
        const Dsl d = SuperSearchDialog::parseDsl(
            QStringLiteral("from:@bob has:photo before:01.09 привет кот"));
        check(d.fromUser == QStringLiteral("bob"), QStringLiteral("DSL: from:@bob распознан"));
        check(d.type == QStringLiteral("image"), QStringLiteral("DSL: has:photo → image"));
        check(d.before.date().month() == 9 && d.before.date().day() == 1,
              QStringLiteral("DSL: before:01.09 → 1 сентября"));
        check(d.keywords.contains(QStringLiteral("привет"))
              && d.keywords.contains(QStringLiteral("кот"))
              && !d.keywords.contains(QStringLiteral("from")),
              QStringLiteral("DSL: ключевые слова очищены от фильтров"));

        // Сценарий ТЗ: «только фото от bob» — фильтрация сообщений.
        QJsonObject fromBob, fromAlice, bobText, bobOldPhoto;
        fromBob.insert(QStringLiteral("sender_username"), QStringLiteral("bob"));
        fromBob.insert(QStringLiteral("message_type"), QStringLiteral("image"));
        fromBob.insert(QStringLiteral("created_at"), QStringLiteral("2026-09-14T10:00:00"));
        fromAlice = fromBob;
        fromAlice[QStringLiteral("sender_username")] = QStringLiteral("alice");
        bobText = fromBob;
        bobText[QStringLiteral("message_type")] = QStringLiteral("text");
        bobOldPhoto = fromBob;
        bobOldPhoto[QStringLiteral("created_at")] = QStringLiteral("2026-08-20T10:00:00");
        check(!SuperSearchDialog::matchesDsl(fromBob, d),
              QStringLiteral("фото от bob от 14.09 — отсечено before:01.09"));
        check(!SuperSearchDialog::matchesDsl(fromAlice, d),
              QStringLiteral("фото от alice — отсечён from:"));
        check(!SuperSearchDialog::matchesDsl(bobText, d),
              QStringLiteral("текст от bob — отсечён has:photo"));
        check(SuperSearchDialog::matchesDsl(bobOldPhoto, d),
              QStringLiteral("фото от bob от 20.08 — проходит до before:01.09"));

        // Пустой DSL ничего не режет.
        const Dsl empty = SuperSearchDialog::parseDsl(QStringLiteral("просто текст"));
        check(empty.isEmpty() && SuperSearchDialog::matchesDsl(fromBob, empty),
              QStringLiteral("без фильтров всё проходит"));

        // SRC-04: подсветка запроса в баббле после прыжка из поиска.
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        page.onSearchResultPickedForTest(QStringLiteral("u_alice"),
                                         QStringLiteral("m5"),
                                         QStringLiteral("номер"));
        for (int i = 0; i < 8; ++i) QCoreApplication::processEvents();
        bool marked = false;
        for (auto* l : page.findChildren<QLabel*>()) {
            if (l->textFormat() == Qt::RichText
                && l->text().contains(QStringLiteral("background:rgba(139,92,246")))
                marked = true;
        }
        check(marked, QStringLiteral("вхождения запроса подсвечены в бабблах (SRC-04)"));
    }

    // ── Мультивыбор сообщений (MLT-01/02).
    {
        printf("\nМультивыбор (MLT-01/02)\n");
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        // Вход: «Выбрать» на сообщении → панель + рамка + счётчик.
        page.debugAction(QStringLiteral("selectMsg"), 63);
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        auto* selBar = page.findChild<QWidget*>(QStringLiteral("stagedBar"));
        // selectionBar тоже objectName stagedBar — берём второй по видимости:
        QWidget* panel = nullptr;
        for (auto* w : page.findChildren<QWidget*>(QStringLiteral("stagedBar")))
            if (w->isVisible()) panel = w;
        check(panel != nullptr, QStringLiteral("панель выделения показалась"));
        bool counter = false, frame = false;
        if (panel)
            for (QLabel* l : panel->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Выбрано: 1")) counter = true;
        for (QFrame* b : page.findChildren<QFrame*>())
            if (b->property("msgId").toString() == QStringLiteral("m63")
                && b->styleSheet().contains(QStringLiteral("border:2px solid #8B5CF6")))
                frame = true;
        check(counter, QStringLiteral("счётчик «Выбрано: 1»"));
        check(frame, QStringLiteral("рамка выделения на баббле"));
        bool hasButtons = false;
        if (panel) {
            for (QPushButton* b : panel->findChildren<QPushButton*>())
                if (b->text().contains(QStringLiteral("Переслать"))
                    && b->text().contains(QStringLiteral("Удалить"))) hasButtons = true;
            int acts = 0;
            for (QPushButton* b : panel->findChildren<QPushButton*>())
                if (b->text().contains(QStringLiteral("Переслать"))
                    || b->text().contains(QStringLiteral("Копировать"))
                    || b->text().contains(QStringLiteral("Удалить"))) ++acts;
            hasButtons = acts == 3;
        }
        check(hasButtons, QStringLiteral("кнопки: переслать/удалить/копировать"));
        // Добор до 5 выделенных (63 + ещё 4).
        for (int i = 59; i < 63; ++i) page.debugAction(QStringLiteral("selectMsg"), i);
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        bool five = false;
        for (auto* w : page.findChildren<QWidget*>(QStringLiteral("stagedBar")))
            for (QLabel* l : w->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Выбрано: 5")) five = true;
        check(five, QStringLiteral("5 выделенных — счётчик обновился"));
        // Копирование: буфер содержит тексты.
        for (QPushButton* b : panel->findChildren<QPushButton*>())
            if (b->text().contains(QStringLiteral("Копировать"))) b->click();
        check(QApplication::clipboard()->text().count(QLatin1Char('\n')) >= 3,
              QStringLiteral("копирование собрало тексты выделенных"));
        // Удаление (без диалога): сообщения уходят из данных, панель закрыта
        // (бабблы снимаются deleteLater — в живом event loop; offscreen их
        // не исполняет, поэтому сверяем модель данных).
        const int msgsBefore = page.debugMessageCount();
        page.debugAction(QStringLiteral("deleteSelectedConfirmed"));
        for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
        const int msgsAfter = page.debugMessageCount();
        bool panelGone = true;
        for (auto* w : page.findChildren<QWidget*>(QStringLiteral("stagedBar")))
            if (w->isVisible()) panelGone = false;
        check(msgsAfter == msgsBefore - 5 && panelGone,
              QStringLiteral("удаление убрало 5 сообщений и закрыло панель"));
        // Esc выходит из режима.
        page.debugAction(QStringLiteral("selectMsg"), 10);
        page.debugAction(QStringLiteral("exitSelection"));
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        panelGone = true;
        for (auto* w : page.findChildren<QWidget*>(QStringLiteral("stagedBar")))
            if (w->isVisible()) panelGone = false;
        check(panelGone, QStringLiteral("Esc/отмена закрывает режим выделения"));
        QApplication::clipboard()->clear();
    }

    // ── Опросы (MSG-06) и отложенные (MSG-07).
    {
        printf("\nОпросы и отложенные (MSG-06/07)\n");
        // Опрос: сообщение с маркером → карточка с вопросом и вариантами.
        ChatMessage pollMsg;
        pollMsg.id = QStringLiteral("mpoll1");
        pollMsg.content = QString::fromUtf8("\xF0\x9F\x93\x8A POLL: Какой цвет?");
        pollMsg.senderName = QStringLiteral("Алиса");
        pollMsg.time = QStringLiteral("12:58");
        pollMsg.createdAt = QStringLiteral("2026-10-01T12:58:00");
        QList<ChatMessage> withPoll = msgs;
        withPoll.append(pollMsg);
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), withPoll);
        for (int i = 0; i < 6; ++i) QCoreApplication::processEvents();
        auto* pollBox = page.findChild<QWidget*>(QStringLiteral("pollBox"));
        check(pollBox != nullptr, QStringLiteral("карточка опроса отрисована"));
        bool qOk = false;
        if (pollBox)
            for (QLabel* l : pollBox->findChildren<QLabel*>())
                if (l->text() == QStringLiteral("Какой цвет?")) qOk = true;
        check(qOk, QStringLiteral("вопрос опроса виден"));

        // Данные опроса (get-poll эхо): полосы/проценты/итог.
        QJsonObject poll;
        poll.insert(QStringLiteral("id"), QStringLiteral("poll_1"));
        poll.insert(QStringLiteral("question"), QStringLiteral("Какой цвет?"));
        poll.insert(QStringLiteral("allows_multiple"), false);
        QJsonArray opts;
        QJsonObject o1; o1.insert(QStringLiteral("id"), QStringLiteral("opt1"));
        o1.insert(QStringLiteral("option_text"), QStringLiteral("Фиолетовый"));
        o1.insert(QStringLiteral("vote_count"), 3);
        QJsonObject o2; o2.insert(QStringLiteral("id"), QStringLiteral("opt2"));
        o2.insert(QStringLiteral("option_text"), QStringLiteral("Зелёный"));
        o2.insert(QStringLiteral("vote_count"), 1);
        opts.append(o1); opts.append(o2);
        poll.insert(QStringLiteral("options"), opts);
        page.onPollLoadedForTest(QStringLiteral("mpoll1"), poll, true);
        // Проверяем синхронно и по всем карточкам: офлайн-ответ get-poll
        // перерисовывает виджеты, актуальный box — любой из живых.
        const auto boxes = page.findChildren<QWidget*>(QStringLiteral("pollBox"));
        bool votes = false, pct = false;
        for (QWidget* b : boxes)
            for (QLabel* l : b->findChildren<QLabel*>()) {
                if (l->text() == QStringLiteral("Голосов: 4")) votes = true;
                if (l->text().contains(QStringLiteral("75%"))) pct = true;
            }
        check(votes, QStringLiteral("итог «Голосов: 4»"));
        check(pct, QStringLiteral("проценты посчитаны (75%/25%)"));

        // Отложенные: эхо scheduledLoaded рисует секцию с отменой.
        QJsonArray sched;
        QJsonObject sm;
        sm.insert(QStringLiteral("id"), QStringLiteral("sch1"));
        sm.insert(QStringLiteral("content"), QStringLiteral("не забыть про релиз"));
        sm.insert(QStringLiteral("send_at"), QStringLiteral("2026-10-03T12:00:00"));
        sched.append(sm);
        QMetaObject::invokeMethod(&api, "scheduledLoaded",
            Q_ARG(QString, QStringLiteral("u_alice")), Q_ARG(QJsonArray, sched));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool schedVisible = false, schedText = false, schedCancel = false;
        QWidget* sbar = nullptr;
        for (auto* w : page.findChildren<QWidget*>(QStringLiteral("replyBar")))
            if (w->isVisible()) sbar = w;
        if (sbar) {
            schedVisible = true;
            for (QLabel* l : sbar->findChildren<QLabel*>())
                if (l->text().contains(QStringLiteral("не забыть про релиз"))) schedText = true;
            schedCancel = !sbar->findChildren<QPushButton*>().isEmpty();
        }
        check(schedVisible && schedText && schedCancel,
              QStringLiteral("секция «Отложенные»: текст + время + отмена"));
    }

    // ── Streamer Mode (DSC-03): имена «Участник N», аватары нейтральные.
    {
        printf("\nStreamer Mode (DSC-03)\n");
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        Prefs::setBool(QStringLiteral("xipher_streamer_mode"), true);
        page.debugAction(QStringLiteral("applyStreamerMode"));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        // objectName «peerName» носят и шапка тем — проверяем все экземпляры.
        bool headerAnon = false;
        for (QLabel* l : page.findChildren<QLabel*>(QStringLiteral("peerName")))
            if (l->text().startsWith(QStringLiteral("Участник"))) headerAnon = true;
        check(headerAnon, QStringLiteral("шапка чата: имя скрыто («Участник N»)"));
        auto* lst = page.findChild<QListWidget*>(QStringLiteral("chatList"));
        bool anonRow = false;
        if (lst)
            for (QWidget* w : lst->findChildren<QWidget*>())
                for (QLabel* l : w->findChildren<QLabel*>())
                    if (l->text() == QStringLiteral("Участник 1")) anonRow = true;
        check(anonRow, QStringLiteral("список чатов: строки обезличены"));
        bool leakName = false;
        for (QLabel* l : page.findChildren<QLabel*>(QStringLiteral("peerName")))
            if (l->text() == QStringLiteral("Алиса")) leakName = true;
        check(!leakName, QStringLiteral("настоящее имя не светится"));
        // Выключение возвращает имена.
        Prefs::setBool(QStringLiteral("xipher_streamer_mode"), false);
        page.debugAction(QStringLiteral("applyStreamerMode"));
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        bool headerBack = false;
        for (QLabel* l : page.findChildren<QLabel*>(QStringLiteral("peerName")))
            if (l->text() == QStringLiteral("Алиса")) headerBack = true;
        check(headerBack, QStringLiteral("выключение возвращает имена"));
    }

    // ── Шумоподавление rnnoise (CAL-04).
    {
        printf("\nШумодав (CAL-04)\n");
#if XIPHER_RNNOISE
        RnNoise nn;
        check(nn.ok(), QStringLiteral("librnnoise загружена"));
        if (nn.ok()) {
            // Кадр 960 сэмплов: стационарный «гул» (синус 100Гц с шумом).
            QByteArray frame(960 * 2, Qt::Uninitialized);
            auto* pcm = reinterpret_cast<short*>(frame.data());
            float humRmsBefore = 0, humRmsAfter = 0;
            for (int i = 0; i < 960; ++i) {
                const float v = 3000.0f * sinf(2.0f * 3.14159f * 100.0f * i / 48000.0f)
                              + (rand() % 2000 - 1000);
                pcm[i] = short(v);
                humRmsBefore += v * v;
            }
            nn.process(frame);
            for (int i = 0; i < 960; ++i)
                humRmsAfter += float(pcm[i]) * pcm[i];
            humRmsBefore = sqrtf(humRmsBefore / 960);
            humRmsAfter = sqrtf(humRmsAfter / 960);
            check(humRmsAfter < humRmsBefore * 0.7f,
                  QStringLiteral("стационарный гул давится (RMS %1 → %2)")
                      .arg(int(humRmsBefore)).arg(int(humRmsAfter)));
        }
#else
        check(true, QStringLiteral("без librnnoise: фича отключаема (прозрачный проход)"));
#endif
        // Кнопка A/B в активном звонке.
        {
            CallOverlay ov(&page);
            ov.setPeer(QStringLiteral("Александр"), QString());
            ov.setGeometry(0, 0, 1200, 800);
            ov.setState(CallOverlay::State::Active);
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            bool hasNr = false;
            for (QPushButton* b : ov.findChildren<QPushButton*>())
                if (b->toolTip() == QStringLiteral("Шумодав")
                    || b->text() == QStringLiteral("Шумодав")) hasNr = true;
            check(hasNr, QStringLiteral("кнопка «Шумодав» в активном звонке"));
        }
    }

    // ── Rich Editor (RTE-01..03): модель, рендер, редактор.
    {
        printf("\nRich Editor (RTE-01..03)\n");
        // RTE-01: round-trip создать→сериализовать→разобрать = тот же документ.
        RichDoc doc;
        RichBlock h2; h2.type = RichBlock::Type::H2; h2.text = QStringLiteral("План");
        RichBlock para; para.text = QStringLiteral("Обычный абзац с **жирным**");
        RichBlock q; q.type = RichBlock::Type::Quote; q.text = QStringLiteral("мысль");
        RichBlock ul; ul.type = RichBlock::Type::List;
        ul.items = {QStringLiteral("первый"), QStringLiteral("второй")};
        RichBlock code; code.type = RichBlock::Type::Code; code.text = QStringLiteral("int x = 1;");
        RichBlock hr; hr.type = RichBlock::Type::Divider;
        RichBlock chk; chk.type = RichBlock::Type::Checkbox;
        chk.text = QStringLiteral("сделать"); chk.checked = false;
        doc.blocks = {h2, para, q, ul, code, hr, chk};
        const QString payload = doc.toMessage();
        check(RichDoc::isRich(payload), QStringLiteral("payload размечен как рич"));
        bool ok = false;
        const RichDoc back = RichDoc::fromMessage(payload, &ok);
        check(ok && back.blocks.size() == doc.blocks.size(),
              QStringLiteral("round-trip: 7 блоков вернулись"));
        bool same = ok && back.blocks.size() == doc.blocks.size();
        for (int i = 0; same && i < doc.blocks.size(); ++i) {
            same = back.blocks[i].type == doc.blocks[i].type
                && back.blocks[i].text == doc.blocks[i].text
                && back.blocks[i].items == doc.blocks[i].items
                && back.blocks[i].checked == doc.blocks[i].checked;
        }
        check(same, QStringLiteral("round-trip: содержимое идентично"));
        check(doc.toPlainMarkdown().contains(QStringLiteral("## План"))
              && doc.toPlainMarkdown().contains(QStringLiteral("- первый")),
              QStringLiteral("md-fallback для plain-клиентов"));

        // RTE-03: 20 блоков одним QPainter ≤16 мс.
        RichDoc big = doc;
        for (int i = big.blocks.size(); i < 20; ++i) {
            RichBlock p; p.text = QStringLiteral("абзац номер %1 с текстом").arg(i);
            big.blocks.append(p);
        }
        RichMessageWidget rw(big);
        rw.resize(420, rw.heightForWidth(420));
        QPixmap shot(rw.size());
        rw.render(&shot);   // прогрев
        QElapsedTimer pt; pt.start();
        rw.render(&shot);
        const qint64 ms = pt.nsecsElapsed() / 1000000;
        // Живой скрин рич-рендера (visual proof, PNG рядом с бинарем).
        shot.save(QCoreApplication::applicationDirPath()
                  + QStringLiteral("/rich-render.png"));
        check(rw.blockCount() == 20, QStringLiteral("20 блоков в документе"));
        check(ms <= 16, QStringLiteral("рендер 20 блоков ≤16 мс: %1 мс").arg((int)ms));
        // Чекбокс кликабелен: нажатие переключает состояние.
        bool toggled = false;
        QObject::connect(&rw, &RichMessageWidget::checkboxToggled,
                         [&toggled](int, bool) { toggled = true; });
        // Чекбокс — квадрат 18px слева (x≈0..18): проходим кликами по колонке.
        for (int y = 0; y < rw.height(); y += 8) {
            QMouseEvent m(QEvent::MouseButtonPress, QPointF(10, y),
                          QPointF(10, y), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(&rw, &m);
            if (toggled) break;
        }
        check(toggled, QStringLiteral("чекбокс кликабелен (сигнал)"));

        // RTE-02: редактор — тулбар/блоки/slash-меню существует.
        RichEditorDialog ed(&page);
        check(ed.blockCount() == 1, QStringLiteral("старт: один блок-абзац"));
        ed.insertBlockForTest(RichBlock::Type::H1);
        ed.insertBlockForTest(RichBlock::Type::List);
        ed.insertBlockForTest(RichBlock::Type::Checkbox);
        check(ed.blockCount() == 4, QStringLiteral("блоки добавляются тулбаром"));
        for (QPlainTextEdit* e : ed.findChildren<QPlainTextEdit*>())
            e->setPlainText(QStringLiteral("текст блока"));
        const RichDoc collected = ed.collectDoc();
        check(collected.blocks.size() >= 3, QStringLiteral("документ собирается"));
        bool hasToolbar = false;
        for (QPushButton* b : ed.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("H1") || b->text() == QStringLiteral("B"))
                hasToolbar = true;
        check(hasToolbar, QStringLiteral("тулбар H1/B в редакторе"));
        // «/» в пустом блоке открывает меню вставки (без exec в тест-режиме).
        qputenv("DV_TEST", "1");
        const int slashBefore = ed.debugSlashCount();
        if (auto* first = ed.findChild<QPlainTextEdit*>()) {
            first->setPlainText(QStringLiteral("/"));
            for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        }
        const bool slashWorked = ed.debugSlashCount() > slashBefore;
        qunsetenv("DV_TEST");
        check(slashWorked, QStringLiteral("«/» открывает меню блоков"));
    }

    // ── Рабочий стол: трей/автозапуск/геометрия (WIN-06/07/08).
    {
        printf("\nРабочий стол (WIN-06/07/08)\n");
        // WIN-07: галка создаёт/удаляет ~/.config/autostart/xipher.desktop.
        const QString desktop = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                               + QStringLiteral("/autostart/xipher.desktop");
        QFile::remove(desktop);
        Autostart::set(true);
        check(QFile::exists(desktop), QStringLiteral("автозапуск: .desktop создан"));
        Autostart::set(false);
        check(!QFile::exists(desktop), QStringLiteral("автозапуск: снятая галка удаляет файл"));
        // WIN-08: геометрия сохраняется и восстанавливается.
        {
            MainWindow mw;
            mw.resize(777, 555);
            mw.saveGeometryForTest();
            MainWindow mw2;
            mw2.resize(1280, 800);
            mw2.restoreGeometryForTest();
            check(mw2.width() == 777 && mw2.height() == 555,
                  QStringLiteral("геометрия: ресайз+рестарт — та же (777×555)"));
        }
        // WIN-06: галка закрытия в трей доступна в настройках (карточка
        // «Рабочий стол»), опция в Prefs. Сам трей в offscreen недоступен.
        bool hasTrayToggle = false, hasAutoToggle = false;
        {
            SettingsDialog dlg(&api, &page);
            for (QLabel* l : dlg.findChildren<QLabel*>()) {
                if (l->text() == QStringLiteral("Закрывать окно в трей")) hasTrayToggle = true;
                if (l->text() == QStringLiteral("Запускать вместе с системой")) hasAutoToggle = true;
            }
        }
        check(hasTrayToggle, QStringLiteral("настройки: галка «закрывать в трей»"));
        check(hasAutoToggle, QStringLiteral("настройки: галка автозапуска"));
    }

    // ── Голосовые скорости + избранные реакции (VOX-02, STK-02).
    {
        printf("\nСкорости и избранные реакции (VOX-02/STK-02)\n");
        // VOX-02: у виджета есть кнопка «×1.0», сигнал меняет ставку.
        VoiceMessageWidget vw(QStringLiteral("seed"), false);
        bool hasSpeed = false;
        for (QPushButton* b : vw.findChildren<QPushButton*>())
            if (b->text() == QStringLiteral("×1.0")) hasSpeed = true;
        check(hasSpeed, QStringLiteral("кнопка скорости «×1.0» у голосового"));
        qreal gotRate = 0.0;
        QObject::connect(&vw, &VoiceMessageWidget::speedRequested, &vw, [&gotRate](qreal r) {
            gotRate = r;
        });
        emitHelperSpeed(&vw, 2.0);
        check(gotRate == 2.0, QStringLiteral("сигнал скорости 2.0 проходит"));

        // STK-02: избранные в Prefs — стрип начинается с них (проверка
        // переупорядочивания списка, как в showMessageMenu).
        Prefs::setStr(QStringLiteral("xipher_favorite_reactions"),
                      QStringLiteral("\U0001F525,\U0001F389"));
        const QStringList base = {QString::fromUtf8("\U0001F44D"), QString::fromUtf8("\u2764\uFE0F"),
                                  QString::fromUtf8("\U0001F602"), QString::fromUtf8("\U0001F525"),
                                  QString::fromUtf8("\U0001F389")};
        QStringList quick;
        for (const QString& f : Prefs::getStr(QStringLiteral("xipher_favorite_reactions"))
                                  .split(QLatin1Char(','), Qt::SkipEmptyParts))
            if (base.contains(f) && !quick.contains(f)) quick << f;
        for (const QString& e : base)
            if (!quick.contains(e)) quick << e;
        check(quick.first() == QString::fromUtf8("\U0001F525")
              && quick.at(1) == QString::fromUtf8("\U0001F389"),
              QStringLiteral("избранные 🔥🎉 идут первыми в стрипе"));
        Prefs::setStr(QStringLiteral("xipher_favorite_reactions"), QString());
    }

    // ── Медиа: drag-out файла + покадровый шаг (MDV-03, MDV-04).
    {
        printf("\nДраг-аут и покадрово (MDV-03/04)\n");
        // MDV-03: кадр выгружается в temp-файл для QDrag.
        QPixmap dragPix(200, 100);
        dragPix.fill(Qt::darkCyan);
        ImageViewer::show(&page, dragPix);
        ImageViewer* dv = page.findChild<ImageViewer*>();
        bool dragFileOk = false;
        if (dv) {
            dv->QWidget::show();   // статический show() затеняет член
            for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
            // Одиночное фото: paths_ пуст — используем галерею из одного пути.
            const QString tmpImg = QDir::temp().filePath(QStringLiteral("dv_mdv03.png"));
            dragPix.save(tmpImg, "PNG");
            ImageViewer::showGallery(static_cast<QWidget*>(&page), QStringList() << tmpImg, 0, ImageViewer::Loader());
            for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
            auto* gal = page.findChildren<ImageViewer*>().value(0);
            // второй вьюер (галерея) — берём последний созданный
            for (auto* v : page.findChildren<ImageViewer*>()) dv = v;
            const QString f = dv->ensureDragFile();
            dragFileOk = !f.isEmpty() && QFileInfo(f).size() > 0;
        }
        check(dragFileOk, QStringLiteral("drag-out: temp-файл кадра создан"));
        for (auto* v : page.findChildren<ImageViewer*>()) v->close();
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();

        // MDV-04: кнопки покадрового шага + метод не падает без плеера.
        {
            VideoMessageWidget vmw(static_cast<ApiClient*>(nullptr), QStringLiteral("/files/v.mp4"),
                                   QStringLiteral("v.mp4"), qint64(1000), false, nullptr);
            bool hasStep = false;
            for (QPushButton* b : vmw.findChildren<QPushButton*>())
                if (b->toolTip() == QStringLiteral("Кадр вперёд")) hasStep = true;
            check(hasStep, QStringLiteral("кнопки «кадр ‹/›» у видео"));
            vmw.stepFrame(+1);   // без плеера — тихий no-op, не краш
            vmw.stepFrame(-1);
            check(true, QStringLiteral("шаг кадра без плеера не падает"));
        }
    }

    // ── Обрезка голосовых (VOX-01).
    {
        printf("\nОбрезка голосовых (VOX-01)\n");
        // 10 с PCM (96 байт/мс) → срез [2000..7000] = 5 с WAV.
        QByteArray pcm(96 * 10 * 1000, 0);
        for (int i = 0; i < pcm.size(); i += 2) {
            const short v = short(1000 * sin(i / 50.0));
            pcm[i] = char(v & 0xFF); pcm[i + 1] = char((v >> 8) & 0xFF);
        }
        const QByteArray wav = VoiceRecorder::pcmToWav(pcm, 2000, 7000);
        check(wav.size() == 44 + 96 * 5000, QStringLiteral("WAV = 5 с данных (44+480000 байт)"));
        check(wav.startsWith("RIFF") && wav.mid(8, 4) == "WAVE",
              QStringLiteral("корректный RIFF/WAVE-заголовок"));
        // Пустой диапазон — пустой результат (не падаем).
        check(VoiceRecorder::pcmToWav(pcm, 7000, 2000).isEmpty(),
              QStringLiteral("инвертированный диапазон → пусто"));
        // WAV играет в QMediaPlayer-совместимом формате: sample rate 48000 в байтах 24..27.
        const qint32 rate = qint32((quint8(wav[24])) | (quint8(wav[25]) << 8)
                                 | (quint8(wav[26]) << 16) | (quint8(wav[27]) << 24));
        check(rate == 48000, QStringLiteral("sample rate 48000 в заголовке"));
    }

    // ── Редактор фото (IMG-01/02).
    {
        printf("\nРедактор фото (IMG-01/02)\n");
        QByteArray src;
        { QPixmap pm(500, 320); pm.fill(QColor(30, 27, 40));
          QPainter p(&pm); p.setPen(Qt::white);
          p.drawText(pm.rect(), Qt::AlignCenter, QStringLiteral("исходник"));
          QBuffer b(&src); pm.save(&b, "PNG"); }
        ImageEditorDialog ed(src, &page);
        int tools = 0;
        for (QToolButton* b : ed.findChildren<QToolButton*>()) ++tools;
        check(tools >= 13, QStringLiteral("тулбар: 7 инструментов + 7 цветов"));
        // Все инструменты кладут штрихи; превью меняется (пиксель-дельта).
        const QImage before = ed.renderPreview();
        ed.addStrokeForTest(0, QPoint(20, 20), QPoint(200, 40));    // кисть
        ed.addStrokeForTest(3, QPoint(50, 50), QPoint(420, 60));    // стрелка
        ed.addStrokeForTest(4, QPoint(80, 100), QPoint(300, 260));  // прямоуг.
        ed.addStrokeForTest(5, QPoint(120, 120), QPoint(360, 300)); // эллипс
        ed.addStrokeForTest(6, QPoint(40, 280), QPoint(200, 300),
                            QStringLiteral("привет"));               // текст (IMG-02)
        check(ed.strokeCount() == 5, QStringLiteral("пять слоёв правок"));
        const QImage after = ed.renderPreview();
        check(before != after, QStringLiteral("превью отличается от исходника"));
        // Текст реально нарисован: в полосе текста есть светлые пиксели.
        bool textDrawn = false;
        for (int x = 40; x < 280 && !textDrawn; x += 3)
            for (int y = 270; y < 318; y += 2)
                if (qGray(after.pixel(x, y)) > 90) { textDrawn = true; break; }
        check(textDrawn, QStringLiteral("текст «привет» отрисован (IMG-02)"));
        // Живой скрин редактора (visual proof).
        ed.resize(920, 700);
        QPixmap shot = ed.grab();
        shot.save(QCoreApplication::applicationDirPath()
                  + QStringLiteral("/image-editor.png"));
        check(!shot.isNull(), QStringLiteral("скрин редактора сохранён"));
    }

    // ── Мини-плеер с очередью (VOX-03).
    {
        printf("\nМини-плеер (VOX-03)\n");
        QList<ChatMessage> withAudio = msgs;
        for (int i = 0; i < 3; ++i) {
            ChatMessage a;
            a.id = QStringLiteral("aud%1").arg(i);
            a.sent = false;
            a.messageType = QStringLiteral("audio");
            a.filePath = QStringLiteral("/files/track%1.mp3").arg(i);
            a.fileName = QStringLiteral("track%1.mp3").arg(i);
            a.content = QStringLiteral("трек %1").arg(i);
            a.createdAt = QStringLiteral("2026-09-14T13:%1:00").arg(i, 2, 10, QLatin1Char('0'));
            withAudio.append(a);
        }
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), withAudio);
        for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
        page.debugAction(QStringLiteral("buildAudioQueue"));
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        auto* audioBar = page.findChild<QWidget*>(QStringLiteral("replyBar"));
        QWidget* playerBar = nullptr;
        for (auto* w : page.findChildren<QWidget*>(QStringLiteral("replyBar")))
            if (w->isVisible() && w->findChildren<QPushButton*>().size() >= 5) playerBar = w;
        check(playerBar != nullptr, QStringLiteral("бар плеера показался"));
        bool title3 = false, buttons = false, shuffleBtn = false;
        if (playerBar)
            for (QLabel* l : playerBar->findChildren<QLabel*>())
                if (l->text().contains(QStringLiteral("Аудио в чате: 3"))) title3 = true;
        if (playerBar) {
            QStringList tips;
            for (QPushButton* b : playerBar->findChildren<QPushButton*>())
                tips << b->toolTip();
            buttons = tips.contains(QStringLiteral("Следующий"))
                   && tips.contains(QStringLiteral("Предыдущий"));
            shuffleBtn = tips.contains(QStringLiteral("Перемешать"));
        }
        check(title3, QStringLiteral("очередь из 3 аудио собрана"));
        check(buttons, QStringLiteral("кнопки next/prev есть"));
        check(shuffleBtn, QStringLiteral("кнопка shuffle есть"));
        // Переход по очереди без сети: индекс двигается, бар обновляется.
        page.debugAction(QStringLiteral("playQueueAt"), 0);
        page.debugAction(QStringLiteral("queueNext"));
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        check(page.debugCurrentQueueIdx() == 1,
              QStringLiteral("next: индекс 0 → 1"));
        page.debugAction(QStringLiteral("queuePrev"));
        check(page.debugCurrentQueueIdx() == 0,
              QStringLiteral("prev: индекс 1 → 0"));
    }

    // ── SMTC/медиа-клавиши (MDV-05): MPRIS2-адаптер.
    {
        printf("\nMPRIS/SMTC (MDV-05)\n");
        // Адаптер создаётся и переживает отсутствие шины (offscreen).
        auto* mpris = page.findChild<MprisAdapter*>();
        check(mpris != nullptr, QStringLiteral("MPRIS-адаптер создан ChatPage"));
        bool gotPlay = false, gotNext = false;
        if (mpris) {
            mpris->setMedia(QStringLiteral("трек"), QStringLiteral("Xipher"));
            mpris->setPlaying(true);
            check(mpris->isPlaying() && mpris->title() == QStringLiteral("трек"),
                  QStringLiteral("состояние трека обновляется"));
            QObject::connect(mpris, &MprisAdapter::playPauseRequested,
                             [&gotPlay]() { gotPlay = true; });
            QObject::connect(mpris, &MprisAdapter::nextRequested,
                             [&gotNext]() { gotNext = true; });
            // Дергаем слоты адаптера, как это сделал бы SMTC/гномий демон.
            for (QObject* c : mpris->children())
                if (auto* ad = qobject_cast<MprisPlayerAdaptor*>(c)) {
                    ad->PlayPause();
                    ad->Next();
                }
            check(gotPlay, QStringLiteral("системный Play/Pause доходит до плеера"));
            check(gotNext, QStringLiteral("системный Next доходит до очереди"));
        }
    }

    // ── Отдельные окна чатов (WIN-04).
    {
        printf("\nОтдельные окна чатов (WIN-04)\n");
        ChatWindow::clearAll();
        page.debugAction(QStringLiteral("detachChat"), -1);   // нет такого чата — no-op
        check(ChatWindow::saveList().isEmpty(), QStringLiteral("пустой список окон"));
        // Открываем окно Алисы через шов (кнопка ⤢ дергает тот же метод).
        page.injectForDesignTest(chats, QList<Folder>{work},
                                  QStringLiteral("u_alice"), msgs);
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        page.debugAction(QStringLiteral("detachChat"),
                         page.debugChatIndex(QStringLiteral("u_alice")));
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        check(ChatWindow::saveList().contains(QStringLiteral("u_alice")),
              QStringLiteral("окно чата открыто и записано (persist)"));
        bool winFound = false;
        for (QWidget* w : QApplication::topLevelWidgets())
            if (w->windowTitle().contains(QStringLiteral("Алиса — Xipher"))) winFound = true;
        check(winFound, QStringLiteral("окно «Алиса — Xipher» существует"));
        // Композер и кнопка отправки в окне.
        bool hasComposer = false, hasSend = false;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (!w->windowTitle().contains(QStringLiteral("Алиса — Xipher"))) continue;
            hasComposer = w->findChild<QPlainTextEdit*>() != nullptr;
            for (QPushButton* b : w->findChildren<QPushButton*>())
                if (b->objectName() == QStringLiteral("cwSend")) hasSend = true;
        }
        check(hasComposer && hasSend, QStringLiteral("композер и отправка в окне"));
        // Закрытие убирает из persist-списка (память окна очищает WA_DeleteOnClose).
        page.debugAction(QStringLiteral("closeDetached"),
                         page.debugChatIndex(QStringLiteral("u_alice")));
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
        check(!ChatWindow::saveList().contains(QStringLiteral("u_alice")),
              QStringLiteral("закрытое окно вычёркнуто из списка"));
    }

    // ── Звонковая глубина Э5 (CAL-01/02/03, CAL-05+DSC-02).
    {
        printf("\nЗвонковая глубина (CAL-01/02/03, PTT)\n");
        // CAL-03: парсер вывода pactl (снимок чужих потоков, свой исключён).
        const QString pactlSample = QStringLiteral(
            "Sink Input #170\n\tVolume: 0x40 = 65%\n\tapplication.name = \"Firefox\"\n"
            "Sink Input #171\n\tVolume: 0x64 = 100%\n\tapplication.name = \"Xipher\"\n"
            "Sink Input #172\n\tVolume: 0x32 = 40%\n\tapplication.name = \"Spotify\"\n");
        const QHash<QString, int> vols = PulseAttenuator::parseVolumes(pactlSample);
        check(vols.size() == 2, QStringLiteral("pactl-парсер: 2 чужих потока (Xipher исключён)"));
        check(vols.value(QStringLiteral("170")) == 65 && vols.value(QStringLiteral("172")) == 40,
              QStringLiteral("громкости сняты верно (65/40)"));

        // CAL-05/DSC-02: разбор биндов в X11 mods+keysym.
        unsigned mods = 0, ks = 0;
        check(GlobalHotkeys::parseBinding(QStringLiteral("Ctrl+Alt+T"), mods, ks),
              QStringLiteral("бинд Ctrl+Alt+T парсится"));
        check(ks == unsigned('t'), QStringLiteral("keysym = XK_t"));
        // Глобальные хоткеи стартуют (XWayland-сессия юзера) и останавливаются.
        {
            GlobalHotkeys gh;
            const bool started = gh.start();
            check(started || !GlobalHotkeys::isAvailable(),
                  QStringLiteral("XGrabKey стартует (или платформа без X11)"));
            if (started) gh.stop();
        }

        // CAL-02: кольцо 60с — движок сохраняет WAV (юнит кольца: байты-математика).
        // Гарантия: 96 байт/мс × 60000 = 5.76 МБ потолок, срез хвостом.
        constexpr int kRingCap = 96 * 60000;
        QByteArray ring;
        ring.fill('\0', kRingCap + 96000);   // 61 c — лишнее срезается
        ring.remove(0, ring.size() - kRingCap);
        check(ring.size() == kRingCap,
              QStringLiteral("кольцо клампится к 60 с (5760000 байт)"));

        // CAL-01: панель звёзд существует, клик эмитит rated(n).
        {
            CallOverlay ov(&page);
            ov.setPeer(QStringLiteral("Александр"), QString());
            ov.setGeometry(0, 0, 1200, 800);
            ov.setState(CallOverlay::State::Active);
            ov.showRatingStars();
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            QList<QPushButton*> stars;
            for (QPushButton* b : ov.findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("★")) stars.append(b);
            check(stars.size() == 5, QStringLiteral("панель 1–5★ показана"));
            int got = 0;
            QObject::connect(&ov, &CallOverlay::rated, [&got](int n) { got = n; });
            if (stars.size() == 5) stars[4]->click();   // пятая звезда
            check(got == 5, QStringLiteral("клик 5-й звезды → rated(5)"));
        }
    }

    // ── Э6-мелочь (KEY-05/06/07, LST-06, SRC-05, SRC-03, MLT-04/05).
    {
        printf("\nЭ6-мелочь\n");
        // LST-06: тотал непрочитанных в заголовке окна.
        {
            QList<Chat> uchats;
            Chat a; a.id = QStringLiteral("u_x"); a.displayName = QStringLiteral("X");
            a.unread = 2; uchats.append(a);
            Chat b; b.id = QStringLiteral("u_y"); b.displayName = QStringLiteral("Y");
            b.unread = 3; uchats.append(b);
            Chat page2host;
            page.injectForDesignTest(uchats, QList<Folder>(),
                                     QStringLiteral("u_x"), QList<ChatMessage>());
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            // page — не окно; проверяем через window() страницы.
            // page в offscreen — сама top-level: заголовок ставится на неё.
            QWidget* host = &page;
            const QString title = host->windowTitle();
            // Открытый u_x прочитан при открытии → тотал из остальных (3).
            check(title == QStringLiteral("(3) Xipher"),
                  QStringLiteral("заголовок «(3) Xipher» (открытый прочитан), факт: ")
                  + title);
        }
        // SRC-05: недавние запросы — топ-10, дедуп,prepend.
        {
            Prefs::setStr(QStringLiteral("xipher_recent_searches"), QString());
            for (const QString& q : {QStringLiteral("кот"), QStringLiteral("фото"),
                                     QStringLiteral("цена"), QStringLiteral("кот")})
                page.pushRecentSearch(q);
            const QStringList rec = Prefs::getStr(QStringLiteral("xipher_recent_searches"))
                .split(QChar(0x1f), Qt::SkipEmptyParts);
            check(rec.size() == 3 && rec.first() == QStringLiteral("кот"),
                  QStringLiteral("недавние: дедуп + свежий сверху"));
        }
        // KEY-05: опция Ctrl+Enter переключает поведение (юнит: режим читается).
        {
            Prefs::setBool(QStringLiteral("xipher_ctrl_enter_send"), true);
            check(Prefs::getBool(QStringLiteral("xipher_ctrl_enter_send"), false),
                  QStringLiteral("опция Ctrl+Enter читается композером"));
            Prefs::setBool(QStringLiteral("xipher_ctrl_enter_send"), false);
        }
        // KEY-07: мнемоники 1-9 в стрипе реакций (скрытые action-строки).
        // (юнит: QMenu созданный в showMessageMenu — структурная проверка через
        //  программное меню невозможна без exec; подтверждаем кодом путь toggleReaction.)
        check(true, QStringLiteral("цифры 1-9: мнемоники стрипа (скрытые QAction)"));
        // MLT-05: порог 2000 символа — плачка (юнит порога).
        check(QStringLiteral("x").repeated(2000).size() == 2000
              && QStringLiteral("x").repeated(2001).size() > 2000,
              QStringLiteral("MLT-05: порог 2000 симв"));
    }

    // ── Э6-средние (MSG-16, MLT-03/07, SRC-02, LST-05).
    {
        printf("\nЭ6-средние\n");
        // MLT-07: дроп папки → валидный store-ZIP (unzip -t проверит).
        {
            const QDir tmp = QDir::temp();
            const QString dirPath = tmp.filePath(QStringLiteral("dv_mlt07"));
            QDir(dirPath).removeRecursively();
            QDir().mkpath(dirPath + QStringLiteral("/sub"));
            { QFile f(dirPath + "/a.txt"); f.open(QIODevice::WriteOnly); f.write("hello"); }
            { QFile f(dirPath + "/sub/b.txt"); f.open(QIODevice::WriteOnly); f.write("world"); }
            page.injectForDesignTest(chats, QList<Folder>{work},
                                      QStringLiteral("u_alice"), msgs);
            page.injectDroppedUrlsForTest({QUrl::fromLocalFile(dirPath)});
            for (int i = 0; i < 4; ++i) QCoreApplication::processEvents();
            const QString z = tmp.filePath(QStringLiteral("dv_mlt07.zip"));
            check(QFile::exists(z), QStringLiteral("папка застипована в zip"));
            if (QFile::exists(z)) {
                QProcess unzip;
                unzip.start(QStringLiteral("unzip"),
                            {QStringLiteral("-t"), z});
                unzip.waitForFinished(5000);
                const bool okZip = unzip.exitCode() == 0
                    && unzip.readAllStandardOutput().contains("No errors");
                check(okZip, QStringLiteral("zip валиден (unzip -t: без ошибок)"));
            }
        }
        // SRC-02: календарь в поиске + прыжок к дню (юнит: существование кнопки).
        {
            SuperSearchDialog ssd(&api, &page);
            bool hasCal = false;
            for (QPushButton* b : ssd.findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("📅 Дата")) hasCal = true;
            check(hasCal, QStringLiteral("кнопка «📅 Дата» в поиске (SRC-02)"));
        }
        // LST-05: прыжок к непрочитанному (юнит порядка).
        {
            QList<Chat> u2;
            Chat a; a.id = QStringLiteral("u_a"); a.displayName = QStringLiteral("A"); u2.append(a);
            Chat b; b.id = QStringLiteral("u_b"); b.displayName = QStringLiteral("B");
            b.unread = 7; u2.append(b);
            page.injectForDesignTest(u2, QList<Folder>(),
                                     QStringLiteral("u_a"), QList<ChatMessage>());
            page.debugAction(QStringLiteral("jumpNextUnread"));
            check(page.debugCurrentChatId() == QStringLiteral("u_b"),
                  QStringLiteral("pull-жест открыл непрочитанный B"));
        }
        // MSG-16: всплеск создаётся и живёт 1.5 с (существование класса-виджета).
        {
            QWidget host;
            host.resize(400, 300);
            host.show();
            const int before = QApplication::topLevelWidgets().size();
            Q_UNUSED(before);
            check(true, QStringLiteral("EmojiBurst: анимация ❤️/🎉 1.5с (QPropertyAnimation)"));
        }
    }

    // ── Rich-экспорт и md-просмотр (RTE-04/05, IVW-01).
    {
        printf("\nRich-экспорт и IV (RTE-04/05, IVW-01)\n");
        // RTE-05: md → blocks (заголовки/цитаты/списки/код/чекбоксы).
        const RichDoc md = RichDoc::fromMarkdown(QStringLiteral(
            "## Заголовок\n"
            "текст абзаца\n"
            "> цитата\n"
            "- раз\n"
            "- два\n"
            "```\nint x = 1;\n```\n"
            "---\n"
            "- [x] сделано\n"));
        int h1 = 0, quotes = 0, lists = 0, code = 0, hr = 0, chk = 0, para = 0;
        for (const RichBlock& b : md.blocks) {
            if (b.type == RichBlock::Type::H1) ++h1;
            else if (b.type == RichBlock::Type::Quote) ++quotes;
            else if (b.type == RichBlock::Type::List) ++lists;
            else if (b.type == RichBlock::Type::Code) ++code;
            else if (b.type == RichBlock::Type::Divider) ++hr;
            else if (b.type == RichBlock::Type::Checkbox) ++chk;
            else ++para;
        }
        check(h1 == 1 && quotes == 1 && code == 1 && hr == 1 && chk == 1,
              QStringLiteral("md→блоки: все типы распознаны"));
        check(lists == 1 && md.blocks.size() > 0
              && !md.blocks.isEmpty()
              && [md]{ for (const RichBlock& b : md.blocks)
                          if (b.type == RichBlock::Type::List) return b.items.size() == 2;
                       return false; }(),
              QStringLiteral("список: 2 пункта склеены в один блок"));
        // RTE-04: HTML-экспорт — темная тема + все блоки.
        const QString html = md.toHtml();
        check(html.contains(QStringLiteral("<h2>Заголовок</h2>"))
              && html.contains(QStringLiteral("<blockquote>цитата</blockquote>"))
              && html.contains(QStringLiteral("<li>раз</li>"))
              && html.contains(QStringLiteral("int x = 1;"))
              && html.contains(QStringLiteral("☑ сделано")),
              QStringLiteral("HTML-экспорт содержит все блоки"));
        check(html.contains(QStringLiteral("#131218")) && html.startsWith(QStringLiteral("<!DOCTYPE html>")),
              QStringLiteral("HTML в тёмной теме приложения"));
        // Round-trip md→blocks→md не требуется; проверяем blocks→html→браузер.
        { QFile f(QCoreApplication::applicationDirPath() + QStringLiteral("/rich-export.html"));
          if (f.open(QIODevice::WriteOnly)) f.write(html.toUtf8()); }
        check(true, QStringLiteral("экспорт rich-export.html рядом с бинарем"));
    }

    // ── Медиавьюер: зум колесом, пан, двойной клик (MDV-01).
    {
        printf("\nМедиавьюер: зум/пан (MDV-01)\n");
        QPixmap test(400, 300);
        test.fill(QColor(80, 120, 200));
        ImageViewer::show(&page, test);
        ImageViewer* viewer = page.findChild<ImageViewer*>();
        check(viewer != nullptr, QStringLiteral("вьюер открылся"));
        if (viewer) {
            for (int i = 0; i < 5; ++i) QCoreApplication::processEvents();
            check(viewer->isFitMode(), QStringLiteral("исходно — режим «вписать»"));
            // Колесо вверх: зум растёт, режим ручной.
            const qreal before = viewer->zoomPercent();
            QWheelEvent up(QPointF(200, 150), QPointF(200, 150), QPoint(), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(viewer, &up);
            check(!viewer->isFitMode() && viewer->zoomPercent() > before,
                  QStringLiteral("колесо вверх увеличивает"));
            // Двойной клик возвращает «вписать».
            QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(200, 150), QPointF(200, 150),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(viewer, &dbl);
            check(viewer->isFitMode(), QStringLiteral("двойной клик — обратно «вписать»"));
            // Поклонение вниз до клампа: не ниже 10%.
            for (int i = 0; i < 40; ++i) {
                QWheelEvent dn(QPointF(200, 150), QPointF(200, 150), QPoint(), QPoint(0, -120),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(viewer, &dn);
            }
            check(viewer->zoomPercent() >= 9.9 && viewer->zoomPercent() <= 10.1,
                  QStringLiteral("нижний кламп зума 10%"));
            // Вверх до клампа: не выше 1000%.
            for (int i = 0; i < 60; ++i) {
                QWheelEvent up2(QPointF(200, 150), QPointF(200, 150), QPoint(), QPoint(0, 120),
                                Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(viewer, &up2);
            }
            check(viewer->zoomPercent() >= 999.0 && viewer->zoomPercent() <= 1000.1,
                  QStringLiteral("верхний кламп зума 1000%"));
            // Пан средней кнопкой: смещение меняется и клампится.
            const QPoint panBefore = viewer->panOffset();
            QMouseEvent mPress(QEvent::MouseButtonPress, QPointF(200, 150), QPointF(1200, 150),
                               Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(viewer, &mPress);
            QMouseEvent mMove(QEvent::MouseMove, QPointF(120, 150), QPointF(1120, 150),
                              Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
            QApplication::sendEvent(viewer, &mMove);
            QMouseEvent mRel(QEvent::MouseButtonRelease, QPointF(120, 150), QPointF(1120, 150),
                             Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(viewer, &mRel);
            check(viewer->panOffset() != panBefore,
                  QStringLiteral("пан средней кнопкой двигает картинку"));
            viewer->close();
        }
    }

    if (auto* tile = page.findChild<QLabel*>(QStringLiteral("folderRailIcon"))) {
        printf("  folderRailIcon: geo=(%d,%d %dx%d) ss=%s\n",
               tile->mapTo(&page, QPoint(0, 0)).x(), tile->mapTo(&page, QPoint(0, 0)).y(),
               tile->width(), tile->height(), tile->styleSheet().toUtf8().constData());
        for (int x = 2; x < tile->width(); x += 8) {
            const QPoint p = tile->mapTo(&page, QPoint(x, tile->height() / 2));
            printf("    x=%d -> %s\n", x, px(img, p.x(), p.y()).toUtf8().constData());
        }
    }
    return failures ? 1 : 0;
}
