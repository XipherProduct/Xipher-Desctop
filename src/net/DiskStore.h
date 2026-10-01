#pragma once
#include <QByteArray>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  DiskStore — простое дисковое хранилище блобов (шифрование полностью удалено
//  из клиента). Тот же интерфейс, что был у SecretStore, — ChatCache (истории)
//  и FileCache (медиа) работают без изменений кода.
//
//  Хранение: AppDataLocation/<ns>/ (Linux: ~/.local/share/Xipher/Desktop/…,
//  Windows: %APPDATA%\Xipher\Desktop\…). Каталог 0700, файлы 0600 — другие
//  пользователи системы читать не могут, содержимое лежит как есть.
//  Имя файла — SHA-256(userId/id): идентификатор не попадает в ФС.
// ─────────────────────────────────────────────────────────────────────────────
class DiskStore {
public:
    explicit DiskStore(QString ns);

    QByteArray load(const QString& id) const;   // пусто = нет/битый
    bool save(const QString& id, const QByteArray& bytes);   // атомарно (QSaveFile)
    bool touch(const QString& id);              // обновить mtime (для LRU-вытеснения)
    void clearAll() const;

    QString dirPath() const;                    // AppDataLocation/<ns>
    qint64  totalBytes() const;                 // суммарный размер файлов каталога

private:
    QString filePath(const QString& id) const;

    QString ns_;
};
