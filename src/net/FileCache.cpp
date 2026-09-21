#include "net/FileCache.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <algorithm>

FileCache& FileCache::instance() {
    static FileCache c;
    return c;
}

bool FileCache::lookup(const QString& id, QByteArray* out) {
    QByteArray data = store_.load(id);
    if (data.isEmpty()) return false;
    store_.touch(id);   // свежее использование — LRU не вытеснит
    *out = data;
    return true;
}

void FileCache::store(const QString& id, const QByteArray& bytes) {
    if (bytes.isEmpty()) return;
    store_.save(id, bytes);
    evictIfNeeded();
}

void FileCache::clearAll() {
    store_.clearAll();
}

void FileCache::evictIfNeeded() {
    qint64 total = store_.totalBytes();
    if (total <= maxBytes_) return;

    // Давно не использованные (по mtime) — вон, пока не влезли в лимит.
    auto infos = QDir(store_.dirPath())
                     .entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    std::sort(infos.begin(), infos.end(),
              [](const QFileInfo& a, const QFileInfo& b) {
                  return a.lastModified() < b.lastModified();
              });
    for (const QFileInfo& fi : infos) {
        if (total <= maxBytes_) break;
        const qint64 sz = fi.size();
        if (QFile::remove(fi.absoluteFilePath())) total -= sz;
    }
}
