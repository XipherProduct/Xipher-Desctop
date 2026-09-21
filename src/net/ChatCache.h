#pragma once
#include "net/Models.h"
#include "net/SecretStore.h"
#include <QList>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  ChatCache — локальный зашифрованный кэш истории чатов.
//  Чат открывается мгновенно: история рендерится из кэша, затем уточняется
//  ответом сервера. Без кэша у пустого на момент загрузки чата не мелькает
//  приветствие «Здесь пока ничего нет».
//
//  Шифрование и хранение — SecretStore («chatcache»): ключи HKDF от сессионного
//  токена (на диске ключа нет), PRF-CTR + encrypt-then-MAC, каталог 0700/файлы
//  0600, атомарная запись. Хранится хвост истории (максимум 500 сообщений).
// ─────────────────────────────────────────────────────────────────────────────
class ChatCache {
public:
    static ChatCache& instance();

    // Пустой список = кэша нет, не расшифровался или повреждён (молча грузим с сервера).
    QList<ChatMessage> load(const QString& peerId);
    void save(const QString& peerId, const QList<ChatMessage>& msgs);
    void clearAll();   // выход из аккаунта

private:
    ChatCache() = default;

    SecretStore store_{QStringLiteral("chatcache")};
};
