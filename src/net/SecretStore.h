#pragma once
#include <QByteArray>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  SecretStore — переиспользуемое зашифрованное хранилище блобов на диске.
//  Фундамент для ChatCache (истории) и FileCache (медиа: фото/аватарки/голос).
//
//  Хранение: AppDataLocation/<ns>/  (Linux: ~/.local/share/Xipher/Desktop/…,
//  Windows: %APPDATA%\Xipher\Desktop\…). Каталог 0700, файлы 0600 — другие
//  пользователи системы читать не могут; содержимое зашифровано.
//
//  Криптосхема (только примитивы Qt — без внешних зависимостей, Linux и Windows):
//    ключи = HKDF (RFC 5869) от сессионного токена; info включает пространство
//    имён ns и userId — ключ на диске не хранится, чужой аккаунт не расшифруется;
//    шифрование = потоковый PRF-CTR: keystream[i] = HMAC-SHA256(encKey, nonce‖counter),
//    конструктивно эквивалентно AES-CTR (HMAC-SHA256 — криптографическая PRF);
//    целостность = encrypt-then-MAC тегом HMAC-SHA256(macKey, заголовок‖шифртекст).
//  Формат файла: "XBS1" | u32be(version) | nonce[16] | ciphertext | tag[32].
//  Имя файла — SHA-256(userId/id): id не попадает в файловую систему.
// ─────────────────────────────────────────────────────────────────────────────
class SecretStore {
public:
    explicit SecretStore(QString ns);

    QByteArray load(const QString& id) const;   // пусто = нет/не расшифровался/битый
    bool save(const QString& id, const QByteArray& bytes);   // атомарно (QSaveFile)
    bool touch(const QString& id);              // обновить mtime (для LRU-вытеснения)
    void clearAll() const;

    QString dirPath() const;                    // AppDataLocation/<ns>
    qint64  totalBytes() const;                 // суммарный размер файлов каталога

private:
    QString filePath(const QString& id) const;
    static bool deriveKeys(const QString& ns, QByteArray* encKey, QByteArray* macKey);

    QString ns_;
};
