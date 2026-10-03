#pragma once
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include "net/Prefs.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Accounts — реестр профилей мультиаккаунта (DSC-04): список {userId, username,
//  token} в Prefs (xipher_accounts). Текущий — ключ xipher_active_account.
//  Смена аккаунта = чистка сессионных кэшей + validateToken нового профиля.
// ─────────────────────────────────────────────────────────────────────────────
namespace Accounts {

struct Profile {
    QString userId;
    QString username;
    QString token;
};

inline QList<Profile> all() {
    QList<Profile> out;
    const QJsonArray arr = QJsonDocument::fromJson(
        Prefs::getStr(QStringLiteral("xipher_accounts")).toUtf8()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Profile p;
        p.userId = o.value(QStringLiteral("user_id")).toString();
        p.username = o.value(QStringLiteral("username")).toString();
        p.token = o.value(QStringLiteral("token")).toString();
        if (!p.userId.isEmpty() && !p.token.isEmpty()) out.append(p);
    }
    return out;
}

inline void save(const QList<Profile>& profiles) {
    QJsonArray arr;
    for (const Profile& p : profiles) {
        arr.append(QJsonObject{
            {QStringLiteral("user_id"), p.userId},
            {QStringLiteral("username"), p.username},
            {QStringLiteral("token"), p.token}});
    }
    Prefs::setStr(QStringLiteral("xipher_accounts"),
                  QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// Записать/обновить профиль (по userId) и сделать активным.
inline void upsertActive(const Profile& p) {
    QList<Profile> list = all();
    bool found = false;
    for (Profile& e : list)
        if (e.userId == p.userId) { e = p; found = true; break; }
    if (!found) list.append(p);
    save(list);
    Prefs::setStr(QStringLiteral("xipher_active_account"), p.userId);
}

inline void remove(const QString& userId) {
    QList<Profile> list = all();
    for (int i = 0; i < list.size(); ++i)
        if (list[i].userId == userId) { list.removeAt(i); break; }
    save(list);
    if (Prefs::getStr(QStringLiteral("xipher_active_account")) == userId)
        Prefs::setStr(QStringLiteral("xipher_active_account"), QString());
}

inline QString activeId() {
    return Prefs::getStr(QStringLiteral("xipher_active_account"));
}

} // namespace Accounts
