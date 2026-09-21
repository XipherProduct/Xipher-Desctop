// chatcache-test.cpp — юнит-тест зашифрованного кэша чатов (src/net/ChatCache).
// Проверяет: раундтрип полей, персистентность, отказ при чужом токене/пользователе,
// детекцию подмены и порчи файла, права 0700/0600, отсутствие открытого текста
// на диске, clearAll, большой объём.
//
// Сборка:
//   g++ -fPIC tests/chatcache-test.cpp src/net/ChatCache.cpp src/net/Session.cpp \
//       -I src -o chatcache-test $(pkg-config --cflags --libs Qt6Core)

#include "net/ChatCache.h"
#include "net/Session.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QStandardPaths>
#include <QTimer>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static ChatMessage sample(int i, const QString& tag = QString()) {
    ChatMessage m;
    m.id = QStringLiteral("msg_%1").arg(i);
    m.senderId = QStringLiteral("u_%1").arg(i % 3);
    m.senderName = QStringLiteral("Имя %1").arg(i);
    m.content = tag + QStringLiteral("Текст сообщения №%1 — тест, 🎉 эмодзи и \"кавычки\" <> & %").arg(i);
    m.messageType = (i % 4 == 0) ? QStringLiteral("image") : QStringLiteral("text");
    m.time = QStringLiteral("12:%1").arg(i, 2, 10, QChar('0'));
    m.createdAt = QDateTime(QDate(2026, 9, 14), QTime(10, 0, i % 60)).toString(Qt::ISODate);
    m.status = QStringLiteral("read");
    m.sent = (i % 2 == 0);
    m.isRead = true;
    m.isDelivered = true;
    m.ttlSeconds = (i % 5 == 0) ? 3600 : 0;
    m.filePath = QStringLiteral("/files/file_%1.jpg").arg(i);
    m.fileName = QStringLiteral("file_%1.jpg").arg(i);
    m.fileSize = 1234567 + i;
    m.replyAuthor = QStringLiteral("Автор");
    m.replySnippet = QStringLiteral("Фрагмент ответа %1").arg(i);
    return m;
}

static bool same(const ChatMessage& a, const ChatMessage& b) {
    return a.id == b.id && a.senderId == b.senderId && a.senderName == b.senderName
        && a.content == b.content && a.messageType == b.messageType && a.time == b.time
        && a.createdAt == b.createdAt && a.status == b.status && a.sent == b.sent
        && a.isRead == b.isRead && a.isDelivered == b.isDelivered && a.ttlSeconds == b.ttlSeconds
        && a.filePath == b.filePath && a.fileName == b.fileName && a.fileSize == b.fileSize
        && a.replyAuthor == b.replyAuthor && a.replySnippet == b.replySnippet;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherTest"));
    QCoreApplication::setApplicationName(QStringLiteral("ChatCacheTest"));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/chatcache");
    QDir(dir).removeRecursively();   // чистый старт

    Session& s = Session::instance();
    s.token = QStringLiteral("test_token_a_0123456789abcdef");
    s.userId = QStringLiteral("user_A");

    // ── 1. Раундтрип ──────────────────────────────────────────────────────────
    printf("1) Раундтрип: save → load, все поля совпадают\n");
    QList<ChatMessage> msgs;
    for (int i = 0; i < 30; ++i) msgs.append(sample(i, QStringLiteral("T1_")));
    ChatCache::instance().save(QStringLiteral("peer_1"), msgs);
    const QList<ChatMessage> back = ChatCache::instance().load(QStringLiteral("peer_1"));
    bool allSame = back.size() == msgs.size();
    for (int i = 0; allSame && i < msgs.size(); ++i) allSame = same(msgs[i], back[i]);
    check(allSame, "30 сообщений — все 17 полей совпали");

    // ── 2. Файл на диске: права, отсутствие plaintext, имя без peerId ───────
    printf("2) Диск: права, шифрование, анонимное имя\n");
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.xbs")}, QDir::Files);
    check(files.size() == 1, "ровно один файл кэша");
    const QString path = dir + QLatin1Char('/') + files.first();
    QFileInfo fi(path);
    const QFile::Permissions p = fi.permissions();
    check((p & (QFile::ReadOwner | QFile::WriteOwner)) != 0
          && (p & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther)) == 0,
          "файл 0600 (только владелец)");
    const QDir parent(fi.absolutePath());
    check(!files.first().contains(QStringLiteral("peer_1")), "имя файла не содержит peerId");
    QFile raw(path);
    raw.open(QIODevice::ReadOnly);
    const QByteArray blob = raw.readAll();
    check(!blob.contains("Текст сообщения") && !blob.contains("msg_1")
          && !blob.contains("peer_1") && !blob.contains("user_A"),
          "открытый текст (контент/ids) в файле отсутствует");
    check(fi.size() > 100, "файл не пустой");
    const QFile::Permissions dp = QFile::permissions(dir);
    check((dp & (QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner)) != 0
          && (dp & (QFile::ReadGroup | QFile::ReadOther | QFile::ExeGroup | QFile::ExeOther)) == 0,
          "каталог 0700 (только владелец)");

    // ── 3. Чужой токен / другой пользователь ────────────────────────────────
    printf("3) Смена аккаунта: кэш не читается\n");
    s.token = QStringLiteral("test_token_b_ffffffffffffffff");
    check(ChatCache::instance().load(QStringLiteral("peer_1")).isEmpty(),
          "другой токен — пусто (тег не сходится)");
    s.token = QStringLiteral("test_token_a_0123456789abcdef");
    s.userId = QStringLiteral("user_B");
    check(ChatCache::instance().load(QStringLiteral("peer_1")).isEmpty(),
          "другой userId — пусто (ключ от другого аккаунта)");
    s.userId = QStringLiteral("user_A");

    // ── 4. Подмена и порча файла ─────────────────────────────────────────────
    printf("4) Целостность: подмена байта, обрезка, мусор\n");
    {
        QByteArray tampered = blob; tampered[tampered.size() / 2] = char(tampered[tampered.size()/2] ^ 0x41);
        QFile tf(path); tf.open(QIODevice::WriteOnly | QIODevice::Truncate); tf.write(tampered); tf.close();
        check(ChatCache::instance().load(QStringLiteral("peer_1")).isEmpty(),
              "перевёрнутый байт шифртекста — пусто (MAC)");
    }
    {
        QByteArray tampered = blob; tampered[tampered.size() - 5] = char(tampered[tampered.size()-5] ^ 0x01);
        QFile tf(path); tf.open(QIODevice::WriteOnly | QIODevice::Truncate); tf.write(tampered); tf.close();
        check(ChatCache::instance().load(QStringLiteral("peer_1")).isEmpty(),
              "подменённый байт тега — пусто (MAC)");
    }
    {
        QFile tf(path); tf.open(QIODevice::WriteOnly | QIODevice::Truncate); tf.write(blob.left(blob.size() - 20)); tf.close();
        check(ChatCache::instance().load(QStringLiteral("peer_1")).isEmpty(), "обрезанный файл — пусто");
        tf.write(QByteArray("garbage not a cache at all")); tf.close();
        check(ChatCache::instance().load(QStringLiteral("peer_1")).isEmpty(), "мусор вместо файла — пусто");
    }
    check(ChatCache::instance().load(QStringLiteral("peer_none")).isEmpty(), "несуществующий чат — пусто");

    // ── 5. Перезапись и большой объём ────────────────────────────────────────
    printf("5) Перезапись, 500 сообщений, тайминги\n");
    {
        QElapsedTimer t; t.start();
        QList<ChatMessage> big;
        for (int i = 0; i < 2000; ++i) big.append(sample(i, QStringLiteral("T5_")));
        ChatCache::instance().save(QStringLiteral("peer_big"), big);
        const qint64 saveMs = t.elapsed();
        t.restart();
        const QList<ChatMessage> bigBack = ChatCache::instance().load(QStringLiteral("peer_big"));
        const qint64 loadMs = t.elapsed();
        check(bigBack.size() == 500, "хранится хвост 500 (лимит)");
        bool tailSame = true;
        for (int i = 0; i < 500; ++i)
            tailSame = tailSame && same(big[big.size() - 500 + i], bigBack[i]);
        check(tailSame, "хвост — последние 500, все поля верны");
        printf("        save(2000)=%lldms load(500)=%lldms\n", saveMs, loadMs);
        check(loadMs < 100, "load быстрее 100 мс (мгновенное открытие)");
    }

    // ── 6. Нет сессии и clearAll ─────────────────────────────────────────────
    printf("6) Без сессии и очистка\n");
    {
        const QString savedToken = s.token;
        s.token.clear();
        ChatCache::instance().save(QStringLiteral("peer_x"), msgs);   // не должно писать/падать
        check(ChatCache::instance().load(QStringLiteral("peer_x")).isEmpty(), "без токена — пусто");
        s.token = savedToken;
        ChatCache::instance().clearAll();
        check(!QDir(dir).exists(), "clearAll удалил каталог");
        check(ChatCache::instance().load(QStringLiteral("peer_big")).isEmpty(),
              "после clearAll чтение — пусто (каталог не пересоздаётся)");
    }

    printf("Итог: %s (%d ошибок)\n", failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ОК", failures);
    return failures ? 1 : 0;
}
