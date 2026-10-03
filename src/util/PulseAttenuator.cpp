#include "util/PulseAttenuator.h"

#include <QProcess>
#include <QRegularExpression>

PulseAttenuator::PulseAttenuator(QObject* parent) : QObject(parent) {}

// Вывод «pactl list sink-inputs»: блоки по потокам. Собираем id → volume %
// и application.name, чтобы не трогать свой поток.
QHash<QString, int> PulseAttenuator::parseVolumes(const QString& out) {
    QHash<QString, int> volumes;
    QString id;
    int vol = -1;
    bool own = false;
    const auto lines = out.split(QLatin1Char('\n'));
    for (const QString& l : lines) {
        const QString t = l.trimmed();
        if (t.startsWith(QStringLiteral("Sink Input #"))) {
            if (!id.isEmpty() && !own && vol >= 0) volumes.insert(id, vol);
            id = t.mid(12).trimmed();
            vol = -1;
            own = false;
        } else if (t.startsWith(QStringLiteral("Volume:"))) {
            // «Volume: 0x40 … 65%» — берём максимум процентных значений.
            static const QRegularExpression re(QStringLiteral("(\\d+)%"));
            auto it = re.globalMatch(t);
            int best = -1;
            while (it.hasNext()) {
                const int v = it.next().captured(1).toInt();
                if (v > best) best = v;
            }
            vol = best;
        } else if (t.startsWith(QStringLiteral("application.name"))) {
            const QString name = t.section(QLatin1Char('='), 1).trimmed()
                                     .remove(QLatin1Char('"'));
            if (name.compare(QStringLiteral("Xipher"), Qt::CaseInsensitive) == 0)
                own = true;
        }
    }
    if (!id.isEmpty() && !own && vol >= 0) volumes.insert(id, vol);
    return volumes;
}

void PulseAttenuator::attenuate(int percent) {
    if (active_) return;
    // Снимок текущих громкостей чужих потоков…
    QProcess* probe = new QProcess(this);
    probe->start(QStringLiteral("pactl"),
                 {QStringLiteral("list"), QStringLiteral("sink-inputs")});
    connect(probe, &QProcess::finished, this, [this, probe, percent](int code) {
        probe->deleteLater();
        if (code != 0) return;   // нет pactl — фича тихо неактивна
        const QHash<QString, int> others = parseVolumes(
            QString::fromUtf8(probe->readAllStandardOutput()));
        if (others.isEmpty()) return;
        savedVolumes_ = others;
        active_ = true;
        applyToOthers(percent);
    });
}

void PulseAttenuator::applyToOthers(int percent) {
    for (auto it = savedVolumes_.constBegin(); it != savedVolumes_.constEnd(); ++it) {
        QProcess::startDetached(QStringLiteral("pactl"),
            {QStringLiteral("set-sink-input-volume"), it.key(),
             QStringLiteral("%1%").arg(percent)});
    }
}

void PulseAttenuator::restore() {
    if (!active_) return;
    active_ = false;
    for (auto it = savedVolumes_.constBegin(); it != savedVolumes_.constEnd(); ++it) {
        QProcess::startDetached(QStringLiteral("pactl"),
            {QStringLiteral("set-sink-input-volume"), it.key(),
             QStringLiteral("%1%").arg(it.value())});
    }
    savedVolumes_.clear();
}
