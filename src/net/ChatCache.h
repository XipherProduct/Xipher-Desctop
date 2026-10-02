#pragma once
#include "net/Models.h"
#include "net/DiskStore.h"
#include <QList>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
//  ChatCache — локальный зашифрованный кэш истории чатов.
//  Чат открывается мгновенно: история рендерится из кэша, затем уточняется
//  ответом сервера. Без кэша у пустого на момент загрузки чата не мелькает
//  приветствие «Здесь пока ничего нет».
//
//  Хранение — DiskStore («chatcache»), без шифрования; права 0700/0600.
class ChatCache {
public:
    static ChatCache& instance();

    // Пустой список = кэша нет, не расшифровался или повреждён (молча грузим с сервера).
    // Удалить кэш одного чата (очистка переписки).
    void remove(const QString& key) { store_.clear(key); }

    QList<ChatMessage> load(const QString& peerId);
    void save(const QString& peerId, const QList<ChatMessage>& msgs);
    void clearAll();   // выход из аккаунта

private:
    ChatCache() = default;

    DiskStore store_{QStringLiteral("chatcache")};   // без шифрования
};
