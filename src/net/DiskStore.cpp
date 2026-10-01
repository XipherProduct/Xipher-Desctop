#include "net/DiskStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

DiskStore::DiskStore(QString ns) : ns_(std::move(ns)) {}

QString DiskStore::dirPath() const {
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QStringLiteral(".");
    const QDir d(base + QLatin1Char('/') + ns_);
    if (!d.exists()) {
        d.mkpath(QStringLiteral("."));
        QFile::setPermissions(d.path(),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    return d.path();
}

QString DiskStore::filePath(const QString& id) const {
    // Имя — SHA-256 от идентификатора: сам id (uuid собеседника, путь медиа)
    // не светится в файловой системе.
    const QByteArray h = QCryptographicHash::hash(
        id.toUtf8(), QCryptographicHash::Sha256).toHex();
    return dirPath() + QLatin1Char('/') + QString::fromLatin1(h) + QStringLiteral(".bin");
}

QByteArray DiskStore::load(const QString& id) const {
    QFile f(filePath(id));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

bool DiskStore::save(const QString& id, const QByteArray& bytes) {
    QSaveFile f(filePath(id));
    if (!f.open(QIODevice::WriteOnly)) return false;
    if (f.write(bytes) != bytes.size()) return false;
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return f.commit();
}

bool DiskStore::touch(const QString& id) {
    // Для LRU надо обновить mtime. QFileDevice::FileTime убрали из Qt 6.11,
    // а пересохранять мегабайты медиа на каждый touch дорого: resize в тот же
    // размер — дешёвая метаданная операция, mtime обновляется на Linux и Windows.
    const QString path = filePath(id);
    const qint64 sz = QFileInfo(path).size();
    if (sz <= 0) return false;
    return QFile::resize(path, sz);
}

void DiskStore::clearAll() const {
    const QDir d(dirPath());
    const auto files = d.entryList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QString& name : files) QFile::remove(d.filePath(name));
}

qint64 DiskStore::totalBytes() const {
    qint64 total = 0;
    const QDir d(dirPath());
    const auto infos = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : infos) total += fi.size();
    return total;
}
