#pragma once
#include "net/SecretStore.h"
#include <QByteArray>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  FileCache — зашифрованный кэш медиа-файлов (фото в чатах, аватарки, голосовые).
//  Фото показываются мгновенно при открытии чата — с диска, без сети; с сервера
//  файл качается только при первом обращении. После выхода из аккаунта кэш
//  очищается (как и кэш историй).
//
//  Хранение — SecretStore («mediacache»): шифрование + права 0700/0600.
//  Лимит объёма с LRU-вытеснением: при переполнении удаляются давно не
//  использовавшиеся файлы (mtime обновляется при каждом чтении).
// ─────────────────────────────────────────────────────────────────────────────
class FileCache {
public:
    static FileCache& instance();

    // Кэш-хит: данные уже локально (мгновенный показ/проигрывание, без сети).
    // mtime файла обновляется — LRU видит свежее использование.
    bool lookup(const QString& id, QByteArray* out);

    // Положить в кэш (после загрузки с сервера).
    void store(const QString& id, const QByteArray& bytes);

    void clearAll();

    // Для тестов: подменить лимит, чтобы проверить вытеснение.
    void setMaxBytesForTesting(qint64 bytes) { maxBytes_ = bytes; }

private:
    FileCache() = default;
    void evictIfNeeded();

    SecretStore store_{QStringLiteral("mediacache")};
    qint64 maxBytes_ = 256 * 1024 * 1024;   // 256 МБ на медиа-кэш
};
