#pragma once
#include <QObject>
#include <QHash>
#include <QStringList>

// ─────────────────────────────────────────────────────────────────────────────
//  PulseAttenuator — приглушение прочих приложений в звонке (CAL-03, Discord-
//  style attenuation): pactl set-sink-input-volume всем чужим потокам -20 дБ,
//  свои (Xipher) не трогаем; после звонка громкости возвращаются.
//  Без pactl/PipeWire (Windows) — тихо неактивен.
// ─────────────────────────────────────────────────────────────────────────────
class PulseAttenuator : public QObject {
    Q_OBJECT
public:
    explicit PulseAttenuator(QObject* parent = nullptr);

    bool isActive() const { return active_; }

    // Вкл/выкл приглушение (возвращает управление громкостям).
    void attenuate(int percent = 20);
    void restore();

    // Тестовые шовы (design-verify): парсер вывода pactl.
    static QHash<QString, int> parseVolumes(const QString& pactlListOutput);

private:
    void applyToOthers(int percent);
    bool active_ = false;
    QHash<QString, int> savedVolumes_;   // sink-input id → прежняя громкость %
};
