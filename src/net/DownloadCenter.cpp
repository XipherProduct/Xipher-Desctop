#include "net/DownloadCenter.h"

#include <QDateTime>

DownloadCenter& DownloadCenter::instance() {
    static DownloadCenter c;
    return c;
}

void DownloadCenter::start(const QString& path, const QString& name, qint64 total) {
    Job& j = jobs_[path];
    j.name = name;
    j.total = total;
    j.received = 0;
    j.done = j.failed = false;
    j.savePath.clear();
    j.lastTickMs = QDateTime::currentMSecsSinceEpoch();
    lastActive_ = path;
    emit changed();
}

void DownloadCenter::progress(const QString& path, qint64 received, qint64 total) {
    if (!jobs_.contains(path)) return;
    Job& j = jobs_[path];
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 dt = now - j.lastTickMs;
    if (dt > 250) {                                   // сглаженная скорость
        const qint64 dBytes = received - j.prevReceived;
        j.speedBps = qMax<qint64>(0, dBytes * 1000 / qMax<qint64>(dt, 1));
        j.prevReceived = received;
        j.lastTickMs = now;
    }
    j.received = received;
    j.total = total > 0 ? total : j.total;
    lastActive_ = path;
    emit changed();
}

void DownloadCenter::finish(const QString& path, const QString& savePath) {
    if (!jobs_.contains(path)) return;
    Job& j = jobs_[path];
    j.done = true;
    j.received = j.total;
    j.speedBps = 0;
    j.savePath = savePath;
    emit finished(path, savePath);
    emit changed();
}

void DownloadCenter::fail(const QString& path) {
    if (!jobs_.contains(path)) return;
    jobs_[path].failed = true;
    emit changed();
}

void DownloadCenter::cancel(const QString& path) {
    jobs_.remove(path);
    emit changed();
}

bool DownloadCenter::hasActive() const {
    for (auto it = jobs_.cbegin(); it != jobs_.cend(); ++it)
        if (!it->done && !it->failed) return true;
    return false;
}

QString DownloadCenter::activePath() const { return lastActive_; }
QString DownloadCenter::activeName() const {
    return jobs_.value(lastActive_).name;
}
int DownloadCenter::activePercent() const {
    const Job j = jobs_.value(lastActive_);
    if (j.total <= 0) return 0;
    return int(qBound<qint64>(qint64(0), j.received * 100 / j.total, qint64(100)));
}
qint64 DownloadCenter::activeSpeedBps() const {
    return jobs_.value(lastActive_).speedBps;
}
qint64 DownloadCenter::activeReceived() const {
    return jobs_.value(lastActive_).received;
}
qint64 DownloadCenter::activeTotal() const {
    return jobs_.value(lastActive_).total;
}

QString DownloadCenter::savedPath(const QString& path) const {
    return jobs_.value(path).savePath;
}
