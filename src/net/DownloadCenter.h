#pragma once
#include <QObject>
#include <QHash>

// ─────────────────────────────────────────────────────────────────────────────
//  DownloadCenter — глобальный реестр загрузок файлов (как панель загрузок в
//  Telegram): живёт поверх чатов, переживает переключение чата, отдаёт
//  состояние нижней панели: имя, процент, скорость, путь готового файла.
// ─────────────────────────────────────────────────────────────────────────────
class DownloadCenter : public QObject {
    Q_OBJECT
public:
    static DownloadCenter& instance();

    void start(const QString& path, const QString& name, qint64 total);
    void progress(const QString& path, qint64 received, qint64 total);
    void finish(const QString& path, const QString& savePath);
    void fail(const QString& path);
    void cancel(const QString& path);

    bool     hasActive() const;
    QString  activePath() const;      // самый свежий активный
    QString  activeName() const;
    int      activePercent() const;
    qint64   activeSpeedBps() const;
    qint64   activeReceived() const;
    qint64   activeTotal() const;

    QString  savedPath(const QString& path) const;   // путь готового файла (или пусто)

signals:
    void changed();
    void finished(const QString& path, const QString& savePath);

private:
    DownloadCenter() = default;

    struct Job {
        QString name;
        qint64  received = 0;
        qint64  total = 0;
        qint64  prevReceived = 0;
        qint64  speedBps = 0;
        qint64  lastTickMs = 0;
        QString savePath;
        bool    done = false;
        bool    failed = false;
    };
    QHash<QString, Job> jobs_;
    QString lastActive_;
};
