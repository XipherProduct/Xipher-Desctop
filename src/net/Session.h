#pragma once
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  Session — текущий вход пользователя. Хранит токен/идентификаторы и умеет
//  персистить их между запусками (QSettings). Аналог localStorage в вебе
//  (ключи xipher_token / xipher_user_id / xipher_username).
// ─────────────────────────────────────────────────────────────────────────────
class Session {
public:
    QString token;
    QString userId;
    QString username;
    bool    isPremium = false;
    QString premiumPlan;
    QString premiumExpiresAt;

    bool isAuthenticated() const { return !token.isEmpty(); }

    void clear();
    void save() const;   // → QSettings
    void load();         // ← QSettings

    static Session& instance();
};
