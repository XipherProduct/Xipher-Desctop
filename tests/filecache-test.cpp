// filecache-test.cpp — юнит-тест зашифрованного кэша медиа (src/net/FileCache).
// Проверяет: раундтрип блобов, промах, вытеснение по LRU (включая обновление
// mtime при чтении), отказ при чужом токене, clearAll.
//
// Сборка (из корня репозитория):
//   g++ -fPIC tests/filecache-test.cpp src/net/FileCache.cpp src/net/SecretStore.cpp \
//       src/net/Session.cpp -I src -o /tmp/fc-test $(pkg-config --cflags --libs Qt6Core)

#include "net/FileCache.h"
#include "net/Session.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <cstdio>

static int failures = 0;
static void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static QByteArray blob(char fill, int size) {
    QByteArray b(size, fill);
    return b;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("XipherTest"));
    QCoreApplication::setApplicationName(QStringLiteral("FileCacheTest"));
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + QStringLiteral("/mediacache");
    QDir(dir).removeRecursively();   // чистый старт

    Session& s = Session::instance();
    s.token = QStringLiteral("test_token_a_0123456789abcdef");
    s.userId = QStringLiteral("user_A");

    // ── 1. Раундтрип и промах ─────────────────────────────────────────────────
    printf("1) Раундтрип и промах\n");
    const QByteArray img = blob('A', 4096);
    FileCache::instance().store(QStringLiteral("/files/photo_1.jpg"), img);
    QByteArray out;
    check(FileCache::instance().lookup(QStringLiteral("/files/photo_1.jpg"), &out)
          && out == img, "store → lookup: байты совпали (фото мгновенно из кэша)");
    check(!FileCache::instance().lookup(QStringLiteral("/files/none.jpg"), &out),
          "нет в кэше — промах (пойдём в сеть)");
    check(!FileCache::instance().lookup(QString(), &out), "пустой ключ — промах");

    // ── 2. Шифрование на диске ───────────────────────────────────────────────
    printf("2) Диск: зашифровано, права\n");
    const QStringList files = QDir(dir).entryList({QStringLiteral("*.xbs")}, QDir::Files);
    check(files.size() == 1, "ровно один файл");
    QFile f(dir + QLatin1Char('/') + files.first());
    f.open(QIODevice::ReadOnly);
    const QByteArray raw = f.readAll();
    check(!raw.contains(QByteArray(64, 'A')), "содержимое (4КБ паттерна 'A') в файле не видно");
    check(raw.size() > 4096, "файл больше полезной нагрузки (заголовок+тег)");
    const QFile::Permissions p = QFileInfo(f).permissions();
    check((p & (QFile::ReadOwner | QFile::WriteOwner)) != 0
          && (p & (QFile::ReadGroup | QFile::ReadOther)) == 0, "файл 0600");

    // ── 3. Чужой токен ───────────────────────────────────────────────────────
    printf("3) Смена аккаунта\n");
    s.token = QStringLiteral("test_token_b_ffffffffffffffff");
    check(!FileCache::instance().lookup(QStringLiteral("/files/photo_1.jpg"), &out),
          "другой токен — промах (кэш не читается)");
    s.token = QStringLiteral("test_token_a_0123456789abcdef");

    // ── 4. LRU-вытеснение ────────────────────────────────────────────────────
    printf("4) LRU: лимит, вытеснение самого старого, touch при чтении\n");
    // Лимит подбираем с учётом оверхеда каждого файла (56 байт: заголовок+тег):
    // 3 блоба по 1 КБ = 3240 байт влезают, 4-й (4320) запускает вытеснение.
    FileCache::instance().setMaxBytesForTesting(4000);
    FileCache::instance().clearAll();
    const QString A = QStringLiteral("/files/a.bin"), B = QStringLiteral("/files/b.bin"),
                  C = QStringLiteral("/files/c.bin"), D = QStringLiteral("/files/d.bin");
    FileCache::instance().store(A, blob('a', 1024));
    QThread::msleep(20);
    FileCache::instance().store(B, blob('b', 1024));
    QThread::msleep(20);
    FileCache::instance().store(C, blob('c', 1024));
    // Читаем A: mtime A обновляется → самый старый теперь B.
    check(FileCache::instance().lookup(A, &out), "A читается");
    QThread::msleep(20);
    FileCache::instance().store(D, blob('d', 1024));   // 4 КБ > 3 КБ → вытеснение B
    const bool aOk = FileCache::instance().lookup(A, &out);
    const bool bGone = !FileCache::instance().lookup(B, &out);
    const bool cOk = FileCache::instance().lookup(C, &out);
    const bool dOk = FileCache::instance().lookup(D, &out);
    check(aOk && cOk && dOk, "A (тронут), C, D живы");
    check(bGone, "B (самый старый без чтения) вытеснен");

    // ── 5. clearAll ──────────────────────────────────────────────────────────
    printf("5) Очистка\n");
    FileCache::instance().setMaxBytesForTesting(256LL * 1024 * 1024);
    FileCache::instance().clearAll();
    check(!QDir(dir).exists() && !FileCache::instance().lookup(A, &out),
          "clearAll удалил всё");

    printf("Итог: %s (%d ошибок)\n", failures ? "ЕСТЬ ОШИБКИ" : "ВСЁ ОК", failures);
    return failures ? 1 : 0;
}
