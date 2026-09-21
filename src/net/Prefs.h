#pragma once
#include <QCoreApplication>
#include <QSettings>
#include <QString>
#include <QVariant>

// ─────────────────────────────────────────────────────────────────────────────
//  Prefs — локальные настройки клиента поверх QSettings (как localStorage в вебе).
//  Тот же бэкенд, что у Session: QSettings("Xipher","Desktop"). Ключи 1:1 с вебом
//  (xipher_notif_*, xipher_calls_*, xipher_language, …), чтобы смысл совпадал.
// ─────────────────────────────────────────────────────────────────────────────
namespace Prefs {

inline QSettings store() {
    // Тесты (design-verify и др.) ставят свой organizationName — их настройки
    // не должны попадать в настройки реального приложения.
    const QString org = QCoreApplication::organizationName();
    if (!org.isEmpty() && org != QStringLiteral("Xipher"))
        return QSettings(org, QStringLiteral("Desktop"));
    return QSettings(QStringLiteral("Xipher"), QStringLiteral("Desktop"));
}

inline QString getStr(const QString& key, const QString& def = QString()) {
    return store().value(key, def).toString();
}
inline void setStr(const QString& key, const QString& val) {
    QSettings s = store(); s.setValue(key, val);
}
inline bool getBool(const QString& key, bool def) {
    return store().value(key, def).toBool();
}
inline void setBool(const QString& key, bool val) {
    QSettings s = store(); s.setValue(key, val);
}
inline int getInt(const QString& key, int def) {
    return store().value(key, def).toInt();
}
inline void setInt(const QString& key, int val) {
    QSettings s = store(); s.setValue(key, val);
}

} // namespace Prefs
