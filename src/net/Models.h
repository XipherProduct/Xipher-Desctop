#pragma once
#include <QString>
#include <QStringList>
#include <QJsonObject>

// ─────────────────────────────────────────────────────────────────────────────
//  Доменные модели чата — повторяют JSON прод-API
//  (/api/chats, /api/messages, /api/send-message).
// ─────────────────────────────────────────────────────────────────────────────

// ICE-сервер для WebRTC. Для STUN username/credential пустые; для TURN —
// эфемерные креды (use-auth-secret/HMAC) из /api/turn-credentials.
// url: "stun:host:port" | "turn:host:port?transport=udp" | "turns:host:port?transport=tcp".
struct IceServerCfg {
    QString url;
    QString username;
    QString credential;
};

// Активная сессия (/api/get-sessions → sessions[]).
struct SessionInfo {
    QString id;          // session_id
    QString userAgent;   // "stealth" / browser UA
    QString createdAt;
    QString lastSeen;
    bool    current = false;
};

// Тип чата (личка / группа / канал).
enum class ChatKind { User = 0, Group = 1, Channel = 2 };

// Папка чатов (синхронизируется с сервером /api/*-chat-folders).
// chatKeys — явный список ключей вида "type:id" (type: chat|saved|group|channel).
struct Folder {
    QString     id;
    QString     name;
    QStringList chatKeys;
    // Как в вебе: у папки свои иконка (ключ из набора приложения) и цвет
    // (#rrggbb) — рейл папок красит плитку в цвет папки.
    QString     icon;
    QString     color;
};

// Публичный элемент каталога (/api/public-directory → items[]).
struct DirectoryItem {
    QString id;
    QString type;          // "channel" | "group"
    QString name;
    QString username;      // @custom_link (опц.)
    QString description;
    QString avatarUrl;
    int     membersCount = 0;
    bool    verified = false;
    bool    isMember = false;
    bool    isPrivate = false;
};

// Элемент списка чатов (/api/chats → chats[]).
struct Chat {
    QString id;            // user_id собеседника (или свой для «Избранных»)
    QString name;          // username
    QString displayName;   // имя для показа (custom_name/имя/username)
    QString avatarUrl;
    QString avatarText;    // эмодзи/буква, если нет аватарки
    QString lastMessage;
    QString time;          // "HH:MM"
    int     unread = 0;
    bool    online = false;
    bool    isSaved = false;   // «Избранные» (saved messages)
    ChatKind kind = ChatKind::User;
    int     membersCount = 0;
    QString customLink;        // @username группы/канала (опц.)
    QString role;              // user_role в группе/канале (creator/admin/member/…)
    bool    isBot = false;
};

// Ключ чата для папок: "type:id" (type: chat|saved|group|channel) — как в вебе.
inline QString chatKeyFor(const Chat& c) {
    QString t = c.isSaved ? QStringLiteral("saved")
              : c.kind == ChatKind::Group   ? QStringLiteral("group")
              : c.kind == ChatKind::Channel ? QStringLiteral("channel")
                                            : QStringLiteral("chat");
    return t + QStringLiteral(":") + c.id;
}

// Тема форума группы (/api/get-group-topics → topics[]).
struct Topic {
    QString id;
    QString name;
    QString iconEmoji;
    QString iconColor;
    bool    isGeneral = false;
    bool    isClosed = false;
    int     pinnedOrder = 0;
    QString lastMessage;
    QString lastSender;
    int     unread = 0;
};

// Участник группы/канала (/api/get-group-members, /api/get-channel-members).
struct Member {
    QString id;        // user_id
    QString username;
    QString role;      // creator | owner | admin | member | subscriber
    bool    isMuted = false;
    bool    isBanned = false;
};

// Результат поиска пользователей (/api/search-users → users[]).
struct UserHit {
    QString id;            // user_id
    QString username;
    QString displayName;
    QString avatarUrl;
    bool    isPremium = false;
    bool    online = false;
    bool    isBot = false;
    bool    isFriend = false;
};

// Входящая заявка в друзья (/api/friend-requests → requests[]).
struct FriendRequest {
    QString id;            // request_id
    QString senderId;
    QString senderUsername;
    QString createdAt;
};

// Сообщение (/api/messages → messages[], /api/send-message ack).
struct ChatMessage {
    QString id;
    QString senderId;
    QString senderName;    // имя отправителя (для групп/каналов)
    QString content;
    QString messageType = QStringLiteral("text");
    QString time;          // "HH:MM"
    QString createdAt;     // ISO
    QString status;        // sent | delivered | read (для исходящих)
    bool    sent = false;  // true → исходящее (моё)
    bool    isRead = false;
    bool    isDelivered = false;
    int     ttlSeconds = 0;   // исчезающее сообщение (0 = обычное)

    // Вложение (voice/file/image): путь вида "/files/<имя>"
    QString filePath;
    QString fileName;
    long long fileSize = 0;

    // Reply-превью (если есть)
    QString replyAuthor;
    QString replySnippet;

    // Разметка бота (reply_markup): inline_keyboard / keyboard / web_app-кнопки.
    // Пустой объект = обычное сообщение без кнопок.
    QJsonObject replyMarkup;

    bool isVoice() const { return messageType == QStringLiteral("voice"); }
};
