#include "net/ApiClient.h"
#include "net/Session.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QDebug>
#include <QHttpMultiPart>
#include <QMutex>
#include <memory>
#include <QMutexLocker>
#include <atomic>
#include <QHttpPart>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

void ApiClient::postJson(const QString& path,
                         const QJsonObject& body,
                         std::function<void(const QJsonObject&, bool, const QString&)> callback) {
    QNetworkRequest req(QUrl(base_ + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    // HTTP/2 у прода режет параллельные стримы ("Server refused a stream") —
    // форсируем HTTP/1.1, иначе поллинг/сигналинг периодически падает.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = nam_->post(req, payload);

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        const QString netErr = reply->error() == QNetworkReply::NoError
                                   ? QString()
                                   : reply->errorString();

        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            // Нет валидного JSON — отдаём то, что есть, как сетевую/серверную ошибку.
            callback(QJsonObject{}, false,
                     netErr.isEmpty() ? QStringLiteral("Некорректный ответ сервера") : netErr);
            return;
        }
        callback(doc.object(), netErr.isEmpty(), netErr);
    });
}

AuthResult ApiClient::parseAuth(const QJsonObject& obj) {
    AuthResult r;
    r.success = obj.value(QStringLiteral("success")).toBool(false);
    r.message = obj.value(QStringLiteral("message")).toString();
    const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
    r.token     = data.value(QStringLiteral("token")).toString();
    r.userId    = data.value(QStringLiteral("user_id")).toString();
    r.username  = data.value(QStringLiteral("username")).toString();
    r.isPremium = data.value(QStringLiteral("is_premium")).toString() == QStringLiteral("true");
    return r;
}

void ApiClient::login(const QString& username, const QString& password) {
    QJsonObject body{{QStringLiteral("username"), username},
                     {QStringLiteral("password"), password}};
    postJson(QStringLiteral("/api/login"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) {
            AuthResult r;
            r.success = false;
            r.message = QStringLiteral("Ошибка соединения с сервером: ") + netErr;
            emit loginFinished(r);
            return;
        }
        emit loginFinished(parseAuth(obj));
    });
}

void ApiClient::registerUser(const QString& username, const QString& password) {
    QJsonObject body{{QStringLiteral("username"), username},
                     {QStringLiteral("password"), password}};
    postJson(QStringLiteral("/api/register"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) {
            AuthResult r;
            r.success = false;
            r.message = QStringLiteral("Ошибка соединения с сервером: ") + netErr;
            emit registerFinished(r);
            return;
        }
        emit registerFinished(parseAuth(obj));
    });
}

void ApiClient::checkUsername(const QString& username) {
    QJsonObject body{{QStringLiteral("username"), username}};
    postJson(QStringLiteral("/api/check-username"), body,
             [this, username](const QJsonObject& obj, bool ok, const QString&) {
        if (!ok && obj.isEmpty()) {
            // Сетевую ошибку при проверке ника не показываем как «занят».
            return;
        }
        const bool available = obj.value(QStringLiteral("success")).toBool(false);
        const QString message = obj.value(QStringLiteral("message")).toString();
        emit usernameChecked(username, available, message);
    });
}

void ApiClient::validateToken(const QString& token) {
    QJsonObject body{{QStringLiteral("token"), token}};
    postJson(QStringLiteral("/api/validate-token"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) {
            AuthResult r;
            r.success = false;
            r.message = netErr;
            emit tokenValidated(r);
            return;
        }
        emit tokenValidated(parseAuth(obj));
    });
}

// ── Чат ──────────────────────────────────────────────────────────────────────

static Chat parseChat(const QJsonObject& o) {
    Chat c;
    c.id          = o.value(QStringLiteral("id")).toString();
    c.name        = o.value(QStringLiteral("name")).toString();
    c.displayName = o.value(QStringLiteral("display_name")).toString();
    if (c.displayName.isEmpty()) c.displayName = c.name;
    c.avatarUrl   = o.value(QStringLiteral("avatar_url")).toString();
    c.avatarText  = o.value(QStringLiteral("avatar")).toString();
    c.lastMessage = o.value(QStringLiteral("lastMessage")).toString();
    c.time        = o.value(QStringLiteral("time")).toString();
    c.unread      = o.value(QStringLiteral("unread")).toInt(0);
    c.online      = o.value(QStringLiteral("online")).toBool(false);
    c.isSaved     = o.value(QStringLiteral("is_saved_messages")).toBool(false);
    c.isBot       = o.value(QStringLiteral("is_bot")).toBool(false);
    return c;
}

// reply_markup приходит строкой с JSON либо готовым объектом (боты, 1:1 с вебом).
static QJsonObject parseReplyMarkup(const QJsonValue& v) {
    if (v.isObject()) return v.toObject();
    if (v.isString()) {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (doc.isObject()) return doc.object();
    }
    return {};
}

static ChatMessage parseMessage(const QJsonObject& o) {
    ChatMessage m;
    m.id          = o.value(QStringLiteral("id")).toString();
    m.senderId    = o.value(QStringLiteral("sender_id")).toString();
    m.content     = o.value(QStringLiteral("content")).toString();
    m.messageType = o.value(QStringLiteral("message_type")).toString();
    if (m.messageType.isEmpty()) m.messageType = QStringLiteral("text");
    m.time        = o.value(QStringLiteral("time")).toString();
    m.createdAt   = o.value(QStringLiteral("created_at")).toString();
    m.status      = o.value(QStringLiteral("status")).toString();
    m.sent        = o.value(QStringLiteral("sent")).toBool(false);
    m.isRead      = o.value(QStringLiteral("is_read")).toBool(false);
    m.isDelivered = o.value(QStringLiteral("is_delivered")).toBool(false);
    m.filePath    = o.value(QStringLiteral("file_path")).toString();
    m.fileName    = o.value(QStringLiteral("file_name")).toString();
    m.fileSize    = static_cast<long long>(o.value(QStringLiteral("file_size")).toDouble(0));
    m.ttlSeconds  = o.value(QStringLiteral("ttl_seconds")).toInt(0);
    const QJsonObject reply = o.value(QStringLiteral("reply")).toObject();
    m.replyAuthor  = reply.value(QStringLiteral("author")).toString();
    m.replySnippet = reply.value(QStringLiteral("snippet")).toString();
    m.replyMarkup  = parseReplyMarkup(o.value(QStringLiteral("reply_markup")));
    return m;
}

void ApiClient::getChats() {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    postJson(QStringLiteral("/api/chats"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("chats"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(true) && obj.contains(QStringLiteral("message"))) {
            emit chatError(QStringLiteral("chats"), obj.value(QStringLiteral("message")).toString());
            return;
        }
        QList<Chat> chats;
        for (const QJsonValue& v : obj.value(QStringLiteral("chats")).toArray())
            chats.append(parseChat(v.toObject()));
        emit chatsLoaded(chats);
    });
}

void ApiClient::getMessages(const QString& friendId, int limit, const QString& beforeId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("friend_id"), friendId}};
    if (limit > 0)   body.insert(QStringLiteral("limit"), limit);
    if (!beforeId.isEmpty()) body.insert(QStringLiteral("before_id"), beforeId);
    postJson(QStringLiteral("/api/messages"), body,
             [this, friendId](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("messages"), netErr); return; }
        QList<ChatMessage> msgs;
        for (const QJsonValue& v : obj.value(QStringLiteral("messages")).toArray())
            msgs.append(parseMessage(v.toObject()));
        emit messagesLoaded(friendId, msgs);
    });
}

void ApiClient::sendMessage(const QString& receiverId, const QString& content, const QString& tempId,
                            int ttlSeconds, const QString& replyTo) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("receiver_id"), receiverId},
                     {QStringLiteral("content"), content},
                     {QStringLiteral("message_type"), QStringLiteral("text")},
                     {QStringLiteral("temp_id"), tempId}};
    if (ttlSeconds > 0) body.insert(QStringLiteral("ttl_seconds"), ttlSeconds);
    if (!replyTo.isEmpty()) body.insert(QStringLiteral("reply_to_message_id"), replyTo);
    postJson(QStringLiteral("/api/send-message"), body,
             [this, receiverId, content, tempId](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("send"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(false)) {
            emit chatError(QStringLiteral("send"),
                obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось отправить")));
            return;
        }
        ChatMessage m;
        m.id          = obj.value(QStringLiteral("message_id")).toString();
        m.senderId    = Session::instance().userId;
        m.content     = obj.value(QStringLiteral("content")).toString(content);
        m.messageType = QStringLiteral("text");
        m.time        = obj.value(QStringLiteral("time")).toString();
        m.createdAt   = obj.value(QStringLiteral("created_at")).toString();
        m.status      = QStringLiteral("sent");
        m.sent        = true;
        emit messageSent(m, receiverId, tempId);
    });
}

void ApiClient::sendRaw(const QString& receiverId, const QString& content,
                        const QString& messageType, const QString& tempId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("receiver_id"), receiverId},
                     {QStringLiteral("content"), content},
                     {QStringLiteral("message_type"), messageType},
                     {QStringLiteral("temp_id"), tempId}};
    postJson(QStringLiteral("/api/send-message"), body,
             [this, receiverId, content, messageType, tempId](
                 const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("send"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(false)) {
            emit chatError(QStringLiteral("send"),
                obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось отправить")));
            return;
        }
        ChatMessage m;
        m.id          = obj.value(QStringLiteral("message_id")).toString();
        m.senderId    = Session::instance().userId;
        m.content     = obj.value(QStringLiteral("content")).toString(content);
        m.messageType = messageType;
        m.time        = obj.value(QStringLiteral("time")).toString();
        m.createdAt   = obj.value(QStringLiteral("created_at")).toString();
        m.status      = QStringLiteral("sent");
        m.sent        = true;
        emit messageSent(m, receiverId, tempId);
    });
}

// ── Люди и друзья ─────────────────────────────────────────────────────────────

void ApiClient::setContactName(const QString& contactId, const QString& customName) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("contact_id"), contactId},
                     {QStringLiteral("custom_name"), customName}};
    postJson(QStringLiteral("/api/set-contact-name"), body,
             [this, contactId, customName](const QJsonObject& obj, bool ok, const QString&) {
        emit contactRenamed(contactId, customName,
                            ok && obj.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::getUserProfile(const QString& userId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("user_id"), userId}};
    postJson(QStringLiteral("/api/get-user-profile"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("profile"), netErr); return; }
        emit profileLoaded(obj);
    });
}

// Профиль v2 — тот же эндпоинт, что и js/profile/view.js веба. Один ответ
// содержит profile + relation + marks + gifts, поэтому клиент не решает сам
// ни приватность, ни набор доступных действий.
void ApiClient::profileView(const QString& userId) {
    const qint64 reqId = ++profileViewReqId_;
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("user_id"), userId}};
    postJson(QStringLiteral("/api/profile/view"), body,
             [this, reqId](const QJsonObject& obj, bool ok, const QString& netErr) {
        emit profileViewLoaded(reqId, obj, ok, netErr);
    });
}

void ApiClient::mediaCount(const QString& chatId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("chat_type"), QStringLiteral("dm")},
                     {QStringLiteral("chat_id"), chatId}};
    postJson(QStringLiteral("/api/media/list"), body,
             [this, chatId](const QJsonObject& obj, bool ok, const QString&) {
        const int total = (ok && obj.value(QStringLiteral("success")).toBool(false))
                              ? obj.value(QStringLiteral("total")).toInt(0) : 0;
        emit mediaCountLoaded(chatId, total);
    });
}

// ── Группы и каналы ───────────────────────────────────────────────────────────

namespace {
QList<ChatMessage> parseGroupChannelMessages(const QJsonObject& obj) {
    QList<ChatMessage> out;
    const QString myId = Session::instance().userId;
    for (const QJsonValue& v : obj.value(QStringLiteral("messages")).toArray()) {
        const QJsonObject o = v.toObject();
        ChatMessage m;
        m.id          = o.value(QStringLiteral("id")).toString();
        m.senderId    = o.value(QStringLiteral("sender_id")).toString();
        m.senderName  = o.value(QStringLiteral("sender_username")).toString();
        m.content     = o.value(QStringLiteral("content")).toString();
        m.messageType = o.value(QStringLiteral("message_type")).toString(QStringLiteral("text"));
        m.time        = o.value(QStringLiteral("time")).toString();
        m.createdAt   = o.value(QStringLiteral("created_at")).toString();
        m.filePath    = o.value(QStringLiteral("file_path")).toString();
        m.fileName    = o.value(QStringLiteral("file_name")).toString();
        m.fileSize    = static_cast<long long>(o.value(QStringLiteral("file_size")).toDouble(0));
        m.replyMarkup = parseReplyMarkup(o.value(QStringLiteral("reply_markup")));
        m.sent        = (m.senderId == myId);
        out.append(m);
    }
    return out;
}
}

void ApiClient::getGroups() {
    postJson(QStringLiteral("/api/get-groups"), {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<Chat> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("groups")).toArray()) {
            const QJsonObject o = v.toObject();
            Chat c;
            c.id          = o.value(QStringLiteral("id")).toString();
            c.name        = o.value(QStringLiteral("name")).toString();
            c.displayName = c.name;
            c.lastMessage = o.value(QStringLiteral("description")).toString();
            c.customLink  = o.value(QStringLiteral("custom_link")).toString();
            c.membersCount= o.value(QStringLiteral("members_count")).toInt();
            c.role        = o.value(QStringLiteral("user_role")).toString();
            c.kind        = ChatKind::Group;
            list.append(c);
        }
        emit groupsLoaded(list);
    });
}

void ApiClient::getChannels() {
    postJson(QStringLiteral("/api/get-channels"), {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<Chat> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("channels")).toArray()) {
            const QJsonObject o = v.toObject();
            Chat c;
            c.id          = o.value(QStringLiteral("id")).toString();
            c.name        = o.value(QStringLiteral("name")).toString();
            c.displayName = c.name;
            c.lastMessage = o.value(QStringLiteral("description")).toString();
            c.customLink  = o.value(QStringLiteral("custom_link")).toString();
            c.avatarUrl   = o.value(QStringLiteral("avatar_url")).toString();
            c.kind        = ChatKind::Channel;
            list.append(c);
        }
        emit channelsLoaded(list);
    });
}

void ApiClient::createGroup(const QString& name, const QString& description) {
    postJson(QStringLiteral("/api/create-group"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("name"), name}, {QStringLiteral("description"), description}},
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        const bool s = ok && obj.value(QStringLiteral("success")).toBool(false);
        emit groupCreated(s, s ? QString() : obj.value(QStringLiteral("error")).toString(
            obj.value(QStringLiteral("message")).toString(netErr)));
    });
}

void ApiClient::createChannel(const QString& name, const QString& description, const QString& customLink) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("name"), name}, {QStringLiteral("description"), description}};
    if (!customLink.isEmpty()) body.insert(QStringLiteral("custom_link"), customLink);
    postJson(QStringLiteral("/api/create-channel"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        const bool s = ok && obj.value(QStringLiteral("success")).toBool(false);
        emit channelCreated(s, s ? QString() : obj.value(QStringLiteral("error")).toString(
            obj.value(QStringLiteral("message")).toString(netErr)));
    });
}

void ApiClient::uploadChannelAvatar(const QString& channelId, const QByteArray& bytes,
                                    const QString& fileName) {
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addField = [multi](const QString& name, const QByteArray& val) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"%1\"").arg(name));
        p.setBody(val);
        multi->append(p);
    };
    addField(QStringLiteral("token"), Session::instance().token.toUtf8());
    addField(QStringLiteral("channel_id"), channelId.toUtf8());

    QHttpPart filePart;
    QString ct = QStringLiteral("image/jpeg");
    if (fileName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) ct = QStringLiteral("image/png");
    else if (fileName.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) ct = QStringLiteral("image/gif");
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, ct);
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"avatar\"; filename=\"%1\"").arg(fileName));
    filePart.setBody(bytes);
    multi->append(filePart);

    QNetworkRequest req(QUrl(base_ + QStringLiteral("/api/upload-channel-avatar")));
    req.setRawHeader("Authorization", QByteArray("Bearer ") + Session::instance().token.toUtf8());
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = nam_->post(req, multi);
    multi->setParent(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        emit channelAvatarUploaded(o.value(QStringLiteral("success")).toBool(!o.value(
            QStringLiteral("avatar_url")).toString().isEmpty()));
    });
}

void ApiClient::getGroupMessages(const QString& groupId, int limit, const QString& beforeId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("group_id"), groupId}, {QStringLiteral("limit"), limit > 0 ? limit : 100}};
    if (!beforeId.isEmpty()) body.insert(QStringLiteral("before_id"), beforeId);
    postJson(QStringLiteral("/api/get-group-messages"),
             body,
             [this, groupId](const QJsonObject& obj, bool, const QString&) {
        emit groupMessagesLoaded(groupId, parseGroupChannelMessages(obj));
    });
}

void ApiClient::getChannelMessages(const QString& channelId, int limit, const QString& beforeId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("channel_id"), channelId}, {QStringLiteral("limit"), limit > 0 ? limit : 100}};
    if (!beforeId.isEmpty()) body.insert(QStringLiteral("before_id"), beforeId);
    postJson(QStringLiteral("/api/get-channel-messages"),
             body,
             [this, channelId](const QJsonObject& obj, bool, const QString&) {
        emit channelMessagesLoaded(channelId, parseGroupChannelMessages(obj));
    });
}

void ApiClient::sendGroupMessage(const QString& groupId, const QString& content, const QString& tempId,
                                 const QString& replyTo) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("group_id"), groupId},
                     {QStringLiteral("content"), content},
                     {QStringLiteral("message_type"), QStringLiteral("text")},
                     {QStringLiteral("temp_id"), tempId}};
    if (!replyTo.isEmpty()) body.insert(QStringLiteral("reply_to_message_id"), replyTo);
    postJson(QStringLiteral("/api/send-group-message"), body, [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::sendChannelMessage(const QString& channelId, const QString& content, const QString& tempId,
                                   const QString& replyTo) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("channel_id"), channelId},
                     {QStringLiteral("content"), content},
                     {QStringLiteral("message_type"), QStringLiteral("text")},
                     {QStringLiteral("temp_id"), tempId}};
    if (!replyTo.isEmpty()) body.insert(QStringLiteral("reply_to_message_id"), replyTo);
    postJson(QStringLiteral("/api/send-channel-message"), body, [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::deleteMessage(const QString& messageId, ChatKind kind, const QString& peerId) {
    QString path = QStringLiteral("/api/delete-message");
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("message_id"), messageId}};
    if (kind == ChatKind::Group)        { path = QStringLiteral("/api/delete-group-message");   body.insert(QStringLiteral("group_id"), peerId); }
    else if (kind == ChatKind::Channel) { path = QStringLiteral("/api/delete-channel-message"); body.insert(QStringLiteral("channel_id"), peerId); }
    postJson(path, body, [this](const QJsonObject& o, bool ok, const QString&) {
        emit messageDeleted(ok && o.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::publicDirectory(const QString& category, const QString& search, int offset) {
    postJson(QStringLiteral("/api/public-directory"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("category"), category.isEmpty() ? QStringLiteral("all") : category},
              {QStringLiteral("search"), search},
              {QStringLiteral("limit"), 20}, {QStringLiteral("offset"), offset}},
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<DirectoryItem> items;
        for (const QJsonValue& v : obj.value(QStringLiteral("items")).toArray()) {
            const QJsonObject o = v.toObject();
            DirectoryItem d;
            d.id           = o.value(QStringLiteral("id")).toString();
            d.type         = o.value(QStringLiteral("type")).toString();
            d.name         = o.value(QStringLiteral("name")).toString();
            d.username     = o.value(QStringLiteral("username")).toString();
            d.description  = o.value(QStringLiteral("description")).toString();
            d.avatarUrl    = o.value(QStringLiteral("avatar_url")).toString();
            d.membersCount = o.value(QStringLiteral("members_count")).toInt();
            d.verified     = o.value(QStringLiteral("verified")).toBool(false);
            d.isMember     = o.value(QStringLiteral("is_member")).toBool(false);
            d.isPrivate    = o.value(QStringLiteral("is_private")).toBool(false);
            items.append(d);
        }
        emit directoryLoaded(items, obj.value(QStringLiteral("has_more")).toBool(false));
    });
}

void ApiClient::joinPublic(const QString& id, const QString& type) {
    const QString path = (type == QStringLiteral("channel"))
        ? QStringLiteral("/api/subscribe-channel") : QStringLiteral("/api/join-group");
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    if (type == QStringLiteral("channel")) body.insert(QStringLiteral("channel_id"), id);
    else { body.insert(QStringLiteral("group_id"), id); body.insert(QStringLiteral("invite_link"), id); }
    postJson(path, body, [this, id](const QJsonObject& obj, bool ok, const QString&) {
        emit publicJoined(ok && obj.value(QStringLiteral("success")).toBool(false), id);
    });
}

// ── Управление каналом/группой ───────────────────────────────────────────────
void ApiClient::getChannelInfo(const QString& channelId) {
    postJson(QStringLiteral("/api/get-channel-info"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("channel_id"), channelId}},
             [this](const QJsonObject& obj, bool, const QString&) { emit channelInfoLoaded(obj); });
}

void ApiClient::getMembers(const QString& peerId, bool isChannel) {
    const QString path = isChannel ? QStringLiteral("/api/get-channel-members")
                                   : QStringLiteral("/api/get-group-members");
    const QString idKey = isChannel ? QStringLiteral("channel_id") : QStringLiteral("group_id");
    postJson(path, {{QStringLiteral("token"), Session::instance().token}, {idKey, peerId}},
             [this, peerId](const QJsonObject& obj, bool, const QString&) {
        QList<Member> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("members")).toArray()) {
            const QJsonObject o = v.toObject();
            Member m;
            m.id       = o.value(QStringLiteral("user_id")).toString();
            m.username = o.value(QStringLiteral("username")).toString();
            m.role     = o.value(QStringLiteral("role")).toString();
            m.isMuted  = o.value(QStringLiteral("is_muted")).toBool(false);
            m.isBanned = o.value(QStringLiteral("is_banned")).toBool(false);
            list.append(m);
        }
        emit membersLoaded(peerId, list);
    });
}

void ApiClient::updatePeerName(const QString& peerId, bool isChannel, const QString& name) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    if (isChannel) { body.insert(QStringLiteral("channel_id"), peerId); body.insert(QStringLiteral("new_name"), name); }
    else           { body.insert(QStringLiteral("group_id"), peerId);   body.insert(QStringLiteral("name"), name); }
    postJson(isChannel ? QStringLiteral("/api/update-channel-name") : QStringLiteral("/api/update-group-name"),
             body, [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false),
                            o.value(QStringLiteral("error")).toString(e));
    });
}

void ApiClient::updatePeerDescription(const QString& peerId, bool isChannel, const QString& desc) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    if (isChannel) { body.insert(QStringLiteral("channel_id"), peerId); body.insert(QStringLiteral("new_description"), desc); }
    else           { body.insert(QStringLiteral("group_id"), peerId);   body.insert(QStringLiteral("description"), desc); }
    postJson(isChannel ? QStringLiteral("/api/update-channel-description") : QStringLiteral("/api/update-group-description"),
             body, [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false),
                            o.value(QStringLiteral("error")).toString(e));
    });
}

void ApiClient::setPeerCustomLink(const QString& peerId, bool isChannel, const QString& link) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("custom_link"), link}};
    body.insert(isChannel ? QStringLiteral("channel_id") : QStringLiteral("group_id"), peerId);
    postJson(isChannel ? QStringLiteral("/api/set-channel-custom-link") : QStringLiteral("/api/set-group-custom-link"),
             body, [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false),
                            o.value(QStringLiteral("error")).toString(e));
    });
}

void ApiClient::unsubscribeChannel(const QString& channelId) {
    postJson(QStringLiteral("/api/unsubscribe-channel"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("channel_id"), channelId}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::leaveGroup(const QString& groupId) {
    postJson(QStringLiteral("/api/leave-group"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("group_id"), groupId}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::deleteChannel(const QString& channelId) {
    postJson(QStringLiteral("/api/delete-channel"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("channel_id"), channelId}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::deleteGroup(const QString& groupId) {
    postJson(QStringLiteral("/api/delete-group"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("group_id"), groupId}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::createInvite(const QString& peerId, bool isChannel) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    body.insert(isChannel ? QStringLiteral("channel_id") : QStringLiteral("group_id"), peerId);
    postJson(isChannel ? QStringLiteral("/api/create-channel-invite") : QStringLiteral("/api/create-group-invite"),
             body, [this](const QJsonObject& o, bool, const QString&) {
        QString tok = o.value(QStringLiteral("token")).toString();
        if (tok.isEmpty()) tok = o.value(QStringLiteral("invite_link")).toString();
        if (tok.isEmpty()) tok = o.value(QStringLiteral("data")).toObject().value(QStringLiteral("invite_link")).toString();
        if (tok.isEmpty()) { emit inviteLinkReady(QString()); return; }
        emit inviteLinkReady(base_ + QStringLiteral("/join/") + tok);
    });
}

void ApiClient::kickGroupMember(const QString& groupId, const QString& userId) {
    postJson(QStringLiteral("/api/kick-member"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("group_id"), groupId}, {QStringLiteral("target_user_id"), userId}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::setGroupMemberRole(const QString& groupId, const QString& userId, const QString& role) {
    postJson(QStringLiteral("/api/set-admin-permissions"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("group_id"), groupId}, {QStringLiteral("target_user_id"), userId},
              {QStringLiteral("role"), role}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::banChannelMember(const QString& channelId, const QString& userId, bool banned) {
    postJson(QStringLiteral("/api/ban-channel-member"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("channel_id"), channelId}, {QStringLiteral("target_user_id"), userId},
              {QStringLiteral("banned"), banned ? QStringLiteral("true") : QStringLiteral("false")}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::muteGroupMember(const QString& groupId, const QString& userId, bool muted) {
    postJson(QStringLiteral("/api/mute-member"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("group_id"), groupId}, {QStringLiteral("target_user_id"), userId},
              {QStringLiteral("muted"), muted ? QStringLiteral("true") : QStringLiteral("false")}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::setGroupPermission(const QString& groupId, const QString& permission, bool enabled) {
    postJson(QStringLiteral("/api/update-group-permissions"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("group_id"), groupId}, {QStringLiteral("permission"), permission},
              {QStringLiteral("enabled"), enabled ? QStringLiteral("true") : QStringLiteral("false")}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::pinMessage(const QString& messageId, ChatKind kind, const QString& peerId, bool pin) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("message_id"), messageId}};
    if (kind == ChatKind::Group)        body.insert(QStringLiteral("group_id"), peerId);
    else if (kind == ChatKind::Channel) body.insert(QStringLiteral("channel_id"), peerId);
    postJson(pin ? QStringLiteral("/api/pin-message") : QStringLiteral("/api/unpin-message"), body,
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit peerActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

// ── Форум-топики ──────────────────────────────────────────────────────────────
void ApiClient::getGroupTopics(const QString& groupId) {
    postJson(QStringLiteral("/api/get-group-topics"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("group_id"), groupId}},
             [this, groupId](const QJsonObject& obj, bool, const QString&) {
        QList<Topic> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("topics")).toArray()) {
            const QJsonObject o = v.toObject();
            Topic t;
            t.id          = o.value(QStringLiteral("id")).toString();
            t.name        = o.value(QStringLiteral("name")).toString();
            t.iconEmoji   = o.value(QStringLiteral("icon_emoji")).toString();
            t.iconColor   = o.value(QStringLiteral("icon_color")).toString();
            t.isGeneral   = o.value(QStringLiteral("is_general")).toBool(false);
            t.isClosed    = o.value(QStringLiteral("is_closed")).toBool(false);
            t.pinnedOrder = o.value(QStringLiteral("pinned_order")).toInt();
            t.lastMessage = o.value(QStringLiteral("last_message")).toString();
            t.lastSender  = o.value(QStringLiteral("last_message_sender")).toString();
            t.unread      = o.value(QStringLiteral("unread_count")).toInt();
            list.append(t);
        }
        emit topicsLoaded(groupId, obj.value(QStringLiteral("forum_mode")).toBool(false), list);
    });
}

void ApiClient::setGroupForumMode(const QString& groupId, bool enabled) {
    postJson(QStringLiteral("/api/set-group-forum-mode"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("group_id"), groupId},
              {QStringLiteral("enabled"), enabled ? QStringLiteral("true") : QStringLiteral("false")}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit topicActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::createGroupTopic(const QString& groupId, const QString& name,
                                 const QString& emoji, const QString& color) {
    postJson(QStringLiteral("/api/create-group-topic"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("group_id"), groupId},
              {QStringLiteral("name"), name}, {QStringLiteral("icon_emoji"), emoji},
              {QStringLiteral("icon_color"), color}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit topicActionDone(ok && o.value(QStringLiteral("success")).toBool(false),
                             o.value(QStringLiteral("error")).toString(e));
    });
}

void ApiClient::updateGroupTopic(const QString& topicId, const QString& name,
                                 const QString& emoji, const QString& color, int closed) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("topic_id"), topicId}};
    if (!name.isEmpty())  body.insert(QStringLiteral("name"), name);
    if (!emoji.isEmpty()) body.insert(QStringLiteral("icon_emoji"), emoji);
    if (!color.isEmpty()) body.insert(QStringLiteral("icon_color"), color);
    if (closed >= 0) body.insert(QStringLiteral("is_closed"), closed ? QStringLiteral("true") : QStringLiteral("false"));
    postJson(QStringLiteral("/api/update-group-topic"), body,
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit topicActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::deleteGroupTopic(const QString& topicId) {
    postJson(QStringLiteral("/api/delete-group-topic"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("topic_id"), topicId}},
             [this](const QJsonObject& o, bool ok, const QString& e) {
        emit topicActionDone(ok && o.value(QStringLiteral("success")).toBool(false), e);
    });
}

void ApiClient::getTopicMessages(const QString& topicId) {
    postJson(QStringLiteral("/api/get-topic-messages"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("topic_id"), topicId}, {QStringLiteral("limit"), 100}},
             [this, topicId](const QJsonObject& obj, bool, const QString&) {
        emit topicMessagesLoaded(topicId, parseGroupChannelMessages(obj));
    });
}

void ApiClient::sendTopicMessage(const QString& topicId, const QString& content, const QString& tempId,
                                 const QString& replyTo) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("topic_id"), topicId},
                     {QStringLiteral("content"), content},
                     {QStringLiteral("message_type"), QStringLiteral("text")},
                     {QStringLiteral("temp_id"), tempId}};
    if (!replyTo.isEmpty()) body.insert(QStringLiteral("reply_to_message_id"), replyTo);
    postJson(QStringLiteral("/api/send-topic-message"), body, [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::getChatFolders() {
    postJson(QStringLiteral("/api/get-chat-folders"), {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<Folder> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("folders")).toArray()) {
            const QJsonObject o = v.toObject();
            Folder f;
            f.id   = o.value(QStringLiteral("id")).toString();
            f.name = o.value(QStringLiteral("name")).toString();
            f.icon  = o.value(QStringLiteral("icon")).toString();
            f.color = o.value(QStringLiteral("color")).toString();
            for (const QJsonValue& k : o.value(QStringLiteral("chat_keys")).toArray())
                f.chatKeys << k.toString();
            list.append(f);
        }
        emit foldersLoaded(list);
    });
}

void ApiClient::setChatFolders(const QList<Folder>& folders) {
    QJsonArray arr;
    for (const Folder& f : folders) {
        QJsonArray keys;
        for (const QString& k : f.chatKeys) keys.append(k);
        arr.append(QJsonObject{{QStringLiteral("id"), f.id},
                               {QStringLiteral("name"), f.name},
                               {QStringLiteral("icon"), f.icon},
                               {QStringLiteral("color"), f.color},
                               {QStringLiteral("chat_keys"), keys}});
    }
    // Сервер ждёт folders как СТРОКУ JSON (как в вебе).
    const QString folded = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    postJson(QStringLiteral("/api/set-chat-folders"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("folders"), folded}},
             [this](const QJsonObject& obj, bool ok, const QString&) {
        emit foldersSaved(ok && obj.value(QStringLiteral("success")).toBool(false));
    });
}

// ── Настройки ───────────────────────────────────────────────────────────────

void ApiClient::getMyProfile() { getUserProfile(Session::instance().userId); }

void ApiClient::updateMyProfile(const QString& firstName, const QString& lastName,
                                const QString& bio, int birthDay, int birthMonth, int birthYear) {
    QJsonObject body{
        {QStringLiteral("token"), Session::instance().token},
        {QStringLiteral("first_name"), firstName},
        {QStringLiteral("last_name"), lastName},
        {QStringLiteral("bio"), bio},
        {QStringLiteral("birth_day"), birthDay},
        {QStringLiteral("birth_month"), birthMonth},
        {QStringLiteral("birth_year"), birthYear}
    };
    postJson(QStringLiteral("/api/update-my-profile"), body,
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        const bool success = ok && obj.value(QStringLiteral("success")).toBool(false);
        emit profileUpdated(success, success ? QString()
            : obj.value(QStringLiteral("message")).toString(netErr));
    });
}

void ApiClient::uploadAvatar(const QByteArray& bytes, const QString& fileName) {
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart tokenPart;
    tokenPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"token\""));
    tokenPart.setBody(Session::instance().token.toUtf8());
    multi->append(tokenPart);

    QHttpPart filePart;
    QString ct = QStringLiteral("image/jpeg");
    if (fileName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) ct = QStringLiteral("image/png");
    else if (fileName.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) ct = QStringLiteral("image/gif");
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, ct);
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"avatar\"; filename=\"%1\"").arg(fileName));
    filePart.setBody(bytes);
    multi->append(filePart);

    QNetworkRequest req(QUrl(base_ + QStringLiteral("/api/upload_avatar")));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + Session::instance().token.toUtf8());
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply* reply = nam_->post(req, multi);
    multi->setParent(reply);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const bool ok = o.value(QStringLiteral("success")).toBool(false);
        QString url = o.value(QStringLiteral("avatar_url")).toString();
        if (url.isEmpty()) url = o.value(QStringLiteral("data")).toObject()
                                   .value(QStringLiteral("avatar_url")).toString();
        emit avatarUploaded(ok, url);
    });
}

void ApiClient::updateMyPrivacy(const QJsonObject& fields) {
    QJsonObject body = fields;
    body.insert(QStringLiteral("token"), Session::instance().token);
    postJson(QStringLiteral("/api/update-my-privacy"), body,
             [this](const QJsonObject& obj, bool ok, const QString&) {
        emit privacyUpdated(ok && obj.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::getSessions() {
    postJson(QStringLiteral("/api/get-sessions"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<SessionInfo> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("sessions")).toArray()) {
            const QJsonObject o = v.toObject();
            SessionInfo s;
            s.id        = o.value(QStringLiteral("session_id")).toString();
            s.userAgent = o.value(QStringLiteral("user_agent")).toString();
            s.createdAt = o.value(QStringLiteral("created_at")).toString();
            s.lastSeen  = o.value(QStringLiteral("last_seen")).toString();
            s.current   = o.value(QStringLiteral("is_current")).toBool(false);
            list.append(s);
        }
        emit sessionsLoaded(list);
    });
}

void ApiClient::revokeSession(const QString& sessionId) {
    postJson(QStringLiteral("/api/revoke-session"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("session_id"), sessionId}},
             [this](const QJsonObject&, bool, const QString&) { emit sessionsChanged(); });
}

void ApiClient::revokeSelectedSessions(const QStringList& ids) {
    QJsonArray arr;
    for (const QString& id : ids) arr.append(id);
    postJson(QStringLiteral("/api/revoke-selected-sessions"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("session_ids"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact))}},
             [this](const QJsonObject&, bool, const QString&) { emit sessionsChanged(); });
}

void ApiClient::revokeOtherSessions() {
    postJson(QStringLiteral("/api/revoke-other-sessions"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject&, bool, const QString&) { emit sessionsChanged(); });
}

void ApiClient::getRecoveryEmail() {
    postJson(QStringLiteral("/api/get-recovery-email"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        emit recoveryEmailLoaded(data.value(QStringLiteral("recovery_email")).toString());
    });
}

void ApiClient::setRecoveryEmail(const QString& email) {
    postJson(QStringLiteral("/api/set-recovery-email"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("email"), email}},
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        const bool success = ok && obj.value(QStringLiteral("success")).toBool(false);
        emit recoveryEmailSaved(success, success ? QString()
            : obj.value(QStringLiteral("message")).toString(netErr));
    });
}

void ApiClient::searchUsers(const QString& query) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("query"), query}};
    postJson(QStringLiteral("/api/search-users"), body,
             [this, query](const QJsonObject& obj, bool ok, const QString&) {
        if (!ok && obj.isEmpty()) return;
        QList<UserHit> users;
        for (const QJsonValue& v : obj.value(QStringLiteral("users")).toArray()) {
            const QJsonObject o = v.toObject();
            UserHit u;
            u.id          = o.value(QStringLiteral("user_id")).toString();
            u.username    = o.value(QStringLiteral("username")).toString();
            u.displayName = o.value(QStringLiteral("display_name")).toString();
            if (u.displayName.isEmpty()) u.displayName = u.username;
            u.avatarUrl   = o.value(QStringLiteral("avatar_url")).toString();
            u.isPremium   = o.value(QStringLiteral("is_premium")).toBool(false);
            u.online      = o.value(QStringLiteral("online")).toBool(o.value(QStringLiteral("is_online")).toBool(false));
            u.isFriend    = o.value(QStringLiteral("is_friend")).toBool(false);
            u.isBot       = o.value(QStringLiteral("is_bot")).toBool(false);
            users.append(u);
        }
        emit usersFound(query, users);
    });
}

void ApiClient::getFriends() {
    postJson(QStringLiteral("/api/friends"), {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<UserHit> list;
        for (const QJsonValue& v : obj.value(QStringLiteral("friends")).toArray()) {
            const QJsonObject o = v.toObject();
            UserHit u;
            u.id          = o.value(QStringLiteral("id")).toString();
            u.username    = o.value(QStringLiteral("username")).toString();
            const QString custom = o.value(QStringLiteral("custom_name")).toString();
            u.displayName = !custom.isEmpty() ? custom
                          : o.value(QStringLiteral("display_name")).toString();
            if (u.displayName.isEmpty()) u.displayName = u.username;
            u.avatarUrl   = o.value(QStringLiteral("avatar_url")).toString();
            u.isPremium   = o.value(QStringLiteral("is_premium")).toBool(false);
            u.online      = o.value(QStringLiteral("online")).toBool(false);
            u.isBot       = o.value(QStringLiteral("is_bot")).toBool(false);
            u.isFriend    = true;
            list.append(u);
        }
        emit friendsLoaded(list);
    });
}

void ApiClient::removeFriend(const QString& userId) {
    postJson(QStringLiteral("/api/remove-friend"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("friend_id"), userId}},
             [this](const QJsonObject& o, bool ok, const QString&) {
        emit contactActionDone(ok && o.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::blockUser(const QString& userId) {
    postJson(QStringLiteral("/api/block-user"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("user_id"), userId}},
             [this](const QJsonObject& o, bool ok, const QString&) {
        emit contactActionDone(ok && o.value(QStringLiteral("success")).toBool(false));
    });
}

// /api/unblock-user — как menu.js «Разблокировать» в вебе.
void ApiClient::unblockUser(const QString& userId) {
    postJson(QStringLiteral("/api/unblock-user"),
             {{QStringLiteral("token"), Session::instance().token}, {QStringLiteral("user_id"), userId}},
             [this](const QJsonObject& o, bool ok, const QString&) {
        emit contactActionDone(ok && o.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::deleteChat(const QString& chatId) {
    postJson(QStringLiteral("/api/delete-chat"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("chat_id"), chatId}, {QStringLiteral("chat_type"), QStringLiteral("chat")}},
             [this](const QJsonObject& o, bool ok, const QString&) {
        emit chatActionDone(ok && o.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::sendFriendRequest(const QString& username) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("username"), username}};
    postJson(QStringLiteral("/api/friend-request"), body,
             [this, username](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit friendRequestSent(username, false, netErr); return; }
        const bool success = obj.value(QStringLiteral("success")).toBool(false);
        emit friendRequestSent(username, success, obj.value(QStringLiteral("message")).toString());
    });
}

void ApiClient::getFriendRequests() {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    postJson(QStringLiteral("/api/friend-requests"), body,
             [this](const QJsonObject& obj, bool ok, const QString&) {
        if (!ok && obj.isEmpty()) return;
        QList<FriendRequest> reqs;
        for (const QJsonValue& v : obj.value(QStringLiteral("requests")).toArray()) {
            const QJsonObject o = v.toObject();
            FriendRequest r;
            r.id             = o.value(QStringLiteral("id")).toString();
            r.senderId       = o.value(QStringLiteral("sender_id")).toString();
            r.senderUsername = o.value(QStringLiteral("sender_username")).toString();
            r.createdAt      = o.value(QStringLiteral("created_at")).toString();
            reqs.append(r);
        }
        emit friendRequestsLoaded(reqs);
    });
}

// ── Голосовые / файлы ─────────────────────────────────────────────────────────

void ApiClient::uploadVoice(const QByteArray& audioBytes, const QString& mimeType, const QString& tempId) {
    QJsonObject body{
        {QStringLiteral("token"), Session::instance().token},
        {QStringLiteral("voice_data"), QString::fromLatin1(audioBytes.toBase64())},
        {QStringLiteral("mime_type"), mimeType}
    };
    postJson(QStringLiteral("/api/upload-voice"), body,
             [this, tempId](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("voice"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(false)) {
            emit chatError(QStringLiteral("voice"),
                obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось загрузить голос")));
            return;
        }
        emit voiceUploaded(obj.value(QStringLiteral("file_path")).toString(),
                           obj.value(QStringLiteral("file_name")).toString(),
                           static_cast<long long>(obj.value(QStringLiteral("file_size")).toDouble(0)),
                           tempId);
    });
}

void ApiClient::sendVoice(const QString& receiverId, const QString& filePath, const QString& fileName,
                          long long fileSize, const QString& caption, const QString& tempId) {
    QJsonObject body{
        {QStringLiteral("token"), Session::instance().token},
        {QStringLiteral("receiver_id"), receiverId},
        {QStringLiteral("content"), caption},
        {QStringLiteral("message_type"), QStringLiteral("voice")},
        {QStringLiteral("file_path"), filePath},
        {QStringLiteral("file_name"), fileName},
        {QStringLiteral("file_size"), static_cast<double>(fileSize)},
        {QStringLiteral("temp_id"), tempId}
    };
    postJson(QStringLiteral("/api/send-message"), body,
             [this, receiverId, filePath, fileName, fileSize, caption, tempId](
                 const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("send"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(false)) {
            emit chatError(QStringLiteral("send"),
                obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось отправить")));
            return;
        }
        ChatMessage m;
        m.id          = obj.value(QStringLiteral("message_id")).toString();
        m.senderId    = Session::instance().userId;
        m.content     = caption;
        m.messageType = QStringLiteral("voice");
        m.filePath    = obj.value(QStringLiteral("file_path")).toString(filePath);
        m.fileName    = fileName;
        m.fileSize    = fileSize;
        m.time        = obj.value(QStringLiteral("time")).toString();
        m.createdAt   = obj.value(QStringLiteral("created_at")).toString();
        m.status      = QStringLiteral("sent");
        m.sent        = true;
        emit messageSent(m, receiverId, tempId);
    });
}

void ApiClient::uploadFile(const QByteArray& bytes, const QString& fileName, const QString& tempId) {
    QJsonObject body{
        {QStringLiteral("token"), Session::instance().token},
        {QStringLiteral("file_data"), QString::fromLatin1(bytes.toBase64())},
        {QStringLiteral("file_name"), fileName}
    };
    postJson(QStringLiteral("/api/upload-file"), body,
             [this, fileName, tempId](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("file"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(false)) {
            emit chatError(QStringLiteral("file"),
                obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось загрузить файл")));
            return;
        }
        emit fileUploaded(obj.value(QStringLiteral("file_path")).toString(),
                          obj.value(QStringLiteral("file_name")).toString(fileName),
                          static_cast<long long>(obj.value(QStringLiteral("file_size")).toDouble(0)),
                          tempId);
    });
}

void ApiClient::sendFile(const QString& receiverId, const QString& filePath, const QString& fileName,
                         long long fileSize, const QString& caption, const QString& tempId,
                         const QString& messageType) {
    QJsonObject body{
        {QStringLiteral("token"), Session::instance().token},
        {QStringLiteral("receiver_id"), receiverId},
        {QStringLiteral("content"), caption},
        {QStringLiteral("message_type"), messageType},
        {QStringLiteral("file_path"), filePath},
        {QStringLiteral("file_name"), fileName},
        {QStringLiteral("file_size"), static_cast<double>(fileSize)},
        {QStringLiteral("temp_id"), tempId}
    };
    postJson(QStringLiteral("/api/send-message"), body,
             [this, receiverId, filePath, fileName, fileSize, caption, tempId, messageType](
                 const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("send"), netErr); return; }
        if (!obj.value(QStringLiteral("success")).toBool(false)) {
            emit chatError(QStringLiteral("send"),
                obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось отправить")));
            return;
        }
        ChatMessage m;
        m.id          = obj.value(QStringLiteral("message_id")).toString();
        m.senderId    = Session::instance().userId;
        m.content     = caption;
        m.messageType = messageType;
        m.filePath    = obj.value(QStringLiteral("file_path")).toString(filePath);
        m.fileName    = fileName;
        m.fileSize    = fileSize;
        m.time        = obj.value(QStringLiteral("time")).toString();
        m.createdAt   = obj.value(QStringLiteral("created_at")).toString();
        m.status      = QStringLiteral("sent");
        m.sent        = true;
        emit messageSent(m, receiverId, tempId);
    });
}

void ApiClient::fetchFile(const QString& filePath) {
    if (fetchReplies_.contains(filePath)) return;   // уже скачивается
    if (parallelReplies_.contains(filePath)) return;
    // Файл, недавно отдавший ошибку (например, 404 — сервер чистит старые),
    // не дёргаем повторно минуту: иначе любой ретрай-цикл UI превращается
    // в шторм запросов.
    if (failedFetches_.contains(filePath)) {
        const qint64 until = failedFetches_.value(filePath);
        if (QDateTime::currentMSecsSinceEpoch() < until) {
            emit fileProgress(filePath, 0, 0);
            return;
        }
        failedFetches_.remove(filePath);
    }
    QNetworkRequest req(QUrl(base_ + filePath));
    req.setRawHeader("Authorization", "Bearer " + Session::instance().token.toUtf8());
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    QNetworkReply* reply = nam_->get(req);
    fetchReplies_.insert(filePath, reply);
    auto* buffer = new QByteArray();
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, filePath](qint64 received, qint64 total) {
        emit fileProgress(filePath, received, total);
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, filePath, buffer]() {
        buffer->append(reply->readAll());
        Q_UNUSED(filePath);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, filePath, buffer]() {
        fetchReplies_.remove(filePath);
        const bool ok = reply->error() == QNetworkReply::NoError;
        if (ok) buffer->append(reply->readAll());
        reply->deleteLater();
        if (!ok) {
            delete buffer;
            failedFetches_.insert(filePath, QDateTime::currentMSecsSinceEpoch() + 60000);
            emit chatError(QStringLiteral("file"), reply->errorString());
            emit fileProgress(filePath, 0, 0);   // «сброшено»
            return;
        }
        const QByteArray bytes = *buffer;
        delete buffer;
        emit fileFetched(filePath, bytes);
    });
}

void ApiClient::fetchFileParallel(const QString& filePath, qint64 total, int chunks) {
    if (total <= 0) { fetchFile(filePath); return; }
    if (parallelReplies_.contains(filePath)) return;   // уже идёт
    if (chunks < 2) { fetchFile(filePath); return; }

    // Чанки по Range, параллельно; собираем в общий буфер по смещениям.
    // ВСЁ общее — только shared_ptr: отмена/обрыв одного чанка не должны
    // оставлять уже поставленные в очередь события на мёртвой памяти.
    auto parts = std::make_shared<QVector<QByteArray>>(chunks);
    auto done = std::make_shared<std::atomic<int>>(0);
    auto failed = std::make_shared<std::atomic<bool>>(false);
    auto mtx = std::make_shared<QMutex>();
    auto received = std::make_shared<std::atomic<qint64>>(0);
    parallelReplies_.insert(filePath, {});   // слот занят

    const qint64 chunk = (total + chunks - 1) / chunks;

    for (int i = 0; i < chunks; ++i) {
        const qint64 s0 = qint64(i) * chunk;
        const qint64 s1 = qMin(total - 1, s0 + chunk - 1);
        if (s0 > s1) { ++(*done); continue; }
        QNetworkRequest req(QUrl(base_ + filePath));
        req.setRawHeader("Authorization", "Bearer " + Session::instance().token.toUtf8());
        req.setRawHeader(QByteArrayLiteral("Range"),
                         QByteArrayLiteral("bytes=") + QByteArray::number(s0)
                         + "-" + QByteArray::number(s1));
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
        QNetworkReply* reply = nam_->get(req);
        parallelReplies_[filePath].append(reply);

        connect(reply, &QNetworkReply::readyRead, this, [this, reply, filePath, parts, i, received, mtx, total]() {
            const QByteArray data = reply->readAll();
            {
                QMutexLocker lk(mtx.get());
                parts->operator[](i).append(data);
            }
            received->fetch_add(data.size());
            emit fileProgress(filePath, received->load(), total);
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, filePath, parts, i, done, failed, received, total, mtx, chunks]() {
            const bool ok = reply->error() == QNetworkReply::NoError
                || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 206;
            if (!ok) failed->store(true);
            else {
                const QByteArray tail = reply->readAll();
                if (!tail.isEmpty()) {
                    QMutexLocker lk(mtx.get());
                    parts->operator[](i).append(tail);
                    received->fetch_add(tail.size());
                }
            }
            reply->deleteLater();
            if (auto lstIt = parallelReplies_.find(filePath); lstIt != parallelReplies_.end())
                lstIt->removeAll(reply);
            if (++(*done) >= chunks) {
                parallelReplies_.remove(filePath);
                QByteArray full;
                full.reserve(total);
                const bool allOk = !failed->load();
                if (allOk) for (const QByteArray& pt : *parts) full += pt;
                if (allOk && full.size() > 0) {
                    emit fileProgress(filePath, total, total);
                    emit fileFetched(filePath, full);
                } else {
                    failedFetches_.insert(filePath, QDateTime::currentMSecsSinceEpoch() + 60000);
                    emit fileProgress(filePath, 0, 0);
                    emit chatError(QStringLiteral("file"), QStringLiteral("Не удалось загрузить файл"));
                }
                // parts/done/failed/mtx/received умрут вместе с последним shared_ptr
            }
        });
    }
}

void ApiClient::cancelFetch(const QString& filePath) {
    if (auto* reply = fetchReplies_.take(filePath)) {
        reply->abort();   // finished придёт с ошибкой → подписчики сбросятся
        reply->deleteLater();
    }
    if (parallelReplies_.contains(filePath)) {
        for (QNetworkReply* r : parallelReplies_.take(filePath)) {
            r->abort();
            r->deleteLater();
        }
        emit fileProgress(filePath, 0, 0);
    }
}

// ── Звонки (сигналинг) ────────────────────────────────────────────────────────

// Веб берёт TURN-креды так: POST /api/turn-credentials (Bearer + {token,ttlMinutes})
// → {username, credential, urls[], expires}. Это эфемерные креды use-auth-secret.
// Прошлая реализация звала только /api/turn-config (отдаёт STUN без кредов) и вдобавок
// выбрасывала username/credential → relay не поднимался, звонок вис на Checking.
void ApiClient::getTurnConfig() {
    const QString token = Session::instance().token;

    QNetworkRequest req(QUrl(base_ + QStringLiteral("/api/turn-credentials")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setRawHeader("Authorization", QByteArray("Bearer ") + token.toUtf8());
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    const QByteArray payload = QJsonDocument(QJsonObject{
        {QStringLiteral("token"), token},
        {QStringLiteral("ttlMinutes"), 60}}).toJson(QJsonDocument::Compact);

    QNetworkReply* reply = nam_->post(req, payload);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString username   = o.value(QStringLiteral("username")).toString();
        const QString credential = o.value(QStringLiteral("credential")).toString();

        if (!username.isEmpty() && !credential.isEmpty()) {
            QStringList urls;
            const QJsonValue uv = o.value(QStringLiteral("urls"));
            if (uv.isString()) urls << uv.toString();
            else for (const QJsonValue& u : uv.toArray()) urls << u.toString();

            QList<IceServerCfg> servers;
            // Доверяем только тому, что отдал сервер (его TURN/STUN). Хост не хардкодим:
            // turn.xipher.pro указывает на старый узел, актуальные urls приходят отсюда.
            for (const QString& u : urls) servers << IceServerCfg{u, username, credential};
            qInfo() << "[call] turn-credentials ok:" << urls << "user" << username;
            emit turnConfigReady(servers);
            return;
        }
        qWarning() << "[call] turn-credentials empty/failed, fallback to turn-config;"
                   << "http err:" << reply->errorString();
        getTurnConfigFallback();
    });
}

void ApiClient::getTurnConfigFallback() {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    postJson(QStringLiteral("/api/turn-config"), body,
             [this](const QJsonObject& obj, bool, const QString&) {
        QList<IceServerCfg> servers;
        for (const QJsonValue& v : obj.value(QStringLiteral("ice_servers")).toArray()) {
            const QJsonObject so = v.toObject();
            const QString user = so.value(QStringLiteral("username")).toString();
            const QString cred = so.value(QStringLiteral("credential")).toString();
            const QJsonValue urls = so.value(QStringLiteral("urls"));
            if (urls.isString()) servers << IceServerCfg{urls.toString(), user, cred};
            else for (const QJsonValue& u : urls.toArray())
                servers << IceServerCfg{u.toString(), user, cred};
        }
        qInfo() << "[call] turn-config fallback, servers:" << servers.size();
        emit turnConfigReady(servers);
    });
}

void ApiClient::callNotify(const QString& receiverId, const QString& callType) {
    postJson(QStringLiteral("/api/call-notification"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), receiverId},
              {QStringLiteral("call_type"), callType}},
             [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::callOffer(const QString& receiverId, const QString& sdp) {
    postJson(QStringLiteral("/api/call-offer"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), receiverId},
              {QStringLiteral("offer"), sdp}},
             [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::callAnswer(const QString& receiverId, const QString& sdp) {
    postJson(QStringLiteral("/api/call-answer"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), receiverId},
              {QStringLiteral("answer"), sdp}},
             [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::callIce(const QString& receiverId, const QString& candidate) {
    postJson(QStringLiteral("/api/call-ice"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), receiverId},
              {QStringLiteral("candidate"), candidate}},
             [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::callEnd(const QString& receiverId) {
    postJson(QStringLiteral("/api/call-end"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), receiverId}},
             [](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::getCallOffer(const QString& callerId) {
    postJson(QStringLiteral("/api/get-call-offer"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), callerId}},   // receiver_id здесь = инициатор
             [this, callerId](const QJsonObject& obj, bool ok, const QString&) {
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        QString sdp = data.value(QStringLiteral("offer")).toString();
        if (sdp.isEmpty()) sdp = obj.value(QStringLiteral("offer")).toString();
        if (!sdp.isEmpty()) emit callOfferReady(callerId, sdp);
    });
}

void ApiClient::getCallAnswer(const QString& calleeId) {
    postJson(QStringLiteral("/api/get-call-answer"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), calleeId}},
             [this, calleeId](const QJsonObject& obj, bool ok, const QString&) {
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        QString sdp = data.value(QStringLiteral("answer")).toString();
        if (sdp.isEmpty()) sdp = obj.value(QStringLiteral("answer")).toString();
        if (!sdp.isEmpty()) emit callAnswerReady(calleeId, sdp);
    });
}

void ApiClient::getCallIce(const QString& otherId) {
    postJson(QStringLiteral("/api/get-call-ice"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("receiver_id"), otherId}},
             [this, otherId](const QJsonObject& obj, bool ok, const QString&) {
        QStringList cands;
        const QJsonValue cv = obj.value(QStringLiteral("data")).toObject().value(QStringLiteral("candidates"));
        // Сервер отдаёт candidates как СТРОКУ с JSON-массивом внутри: "[\"...\"]".
        QJsonArray arr;
        if (cv.isString())      arr = QJsonDocument::fromJson(cv.toString().toUtf8()).array();
        else if (cv.isArray())  arr = cv.toArray();
        else {
            const QJsonValue top = obj.value(QStringLiteral("candidates"));
            if (top.isString())     arr = QJsonDocument::fromJson(top.toString().toUtf8()).array();
            else                    arr = top.toArray();
        }
        for (const QJsonValue& v : arr) {
            if (v.isString()) cands << v.toString();
            else cands << v.toObject().value(QStringLiteral("candidate")).toString();
        }
        if (!cands.isEmpty()) emit callIceBatch(otherId, cands);
    });
}

void ApiClient::checkIncomingCalls() {
    postJson(QStringLiteral("/api/check-incoming-calls"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool ok, const QString&) {
        if (!ok) return;
        const QJsonObject call = obj.value(QStringLiteral("call")).toObject();
        const QString callerId = call.value(QStringLiteral("caller_id")).toString();
        if (callerId.isEmpty()) return;
        emit incomingCall(callerId, call.value(QStringLiteral("caller_username")).toString(),
                          call.value(QStringLiteral("call_type")).toString());
    });
}

void ApiClient::acceptFriend(const QString& requestId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("request_id"), requestId}};
    postJson(QStringLiteral("/api/accept-friend"), body,
             [this, requestId](const QJsonObject& obj, bool ok, const QString&) {
        emit friendActionDone(requestId, true, ok && obj.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::rejectFriend(const QString& requestId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("request_id"), requestId}};
    postJson(QStringLiteral("/api/reject-friend"), body,
             [this, requestId](const QJsonObject& obj, bool ok, const QString&) {
        emit friendActionDone(requestId, false, ok && obj.value(QStringLiteral("success")).toBool(false));
    });
}

// ── Супер-поиск / превью ссылок / сторис (1:1 с supersearch.js, link-preview и stories.src.js веба) ──

void ApiClient::searchMessages(const QString& chatId, const QString& context,
                               const QString& query, const QString& type,
                               int offset, int limit) {    // requestId связывает запрос с ответом (новый запрос отменяет морально старый).
    const QString reqId = QStringLiteral("ss_%1").arg(++searchSeq_);
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("chat_id"), chatId},
                     {QStringLiteral("context"), context}};
    if (!query.isEmpty()) body.insert(QStringLiteral("query"), query);
    if (!type.isEmpty())  body.insert(QStringLiteral("type"), type);
    if (offset > 0) body.insert(QStringLiteral("offset"), QString::number(offset));
    if (limit > 0)  body.insert(QStringLiteral("limit"), QString::number(limit));
    postJson(QStringLiteral("/api/search-messages"), body,
             [this, reqId, chatId](const QJsonObject& obj, bool ok, const QString& netErr) {
        if (!ok && obj.isEmpty()) { emit chatError(QStringLiteral("search"), netErr); return; }
        // Помечаем каждое найденное сообщение чатом запроса: в режиме
        // «Во всех чатах» ответы приходят вперемешку и их надо различать.
        QJsonArray msgs = obj.value(QStringLiteral("messages")).toArray();
        for (QJsonValueRef v : msgs) {
            QJsonObject m = v.toObject();
            m.insert(QStringLiteral("__chat_id"), chatId);
            v = m;
        }
        emit messagesSearched(reqId, msgs);
    });
}

void ApiClient::fetchLinkPreview(const QString& url) {
    postJson(QStringLiteral("/api/link-preview"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("url"), url}},
             [this, url](const QJsonObject& obj, bool, const QString&) {
        emit linkPreviewFetched(url, obj);
    });
}

void ApiClient::loadStories() {
    postJson(QStringLiteral("/api/stories/all"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool, const QString&) {
        emit storiesLoaded(obj);
    });
}

void ApiClient::storyView(const QString& storyId) {
    postJson(QStringLiteral("/api/stories/view"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("story_id"), storyId}},
             [this](const QJsonObject&, bool, const QString&) {});
}

void ApiClient::storyDelete(const QString& storyId) {
    postJson(QStringLiteral("/api/stories/delete"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("story_id"), storyId}},
             [this, storyId](const QJsonObject& obj, bool, const QString&) {
        emit storyDeleted(storyId, obj.value(QStringLiteral("success")).toBool(false));
    });
}

void ApiClient::storyCreate(const QString& mediaUrl, const QString& mediaType,
                            const QString& caption, const QString& privacy,
                            const QString& keyB64, const QString& ivB64) {
    postJson(QStringLiteral("/api/stories/create"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("media_url"), mediaUrl},
              {QStringLiteral("media_type"), mediaType},
              {QStringLiteral("caption"), caption},
              {QStringLiteral("privacy"), privacy},
              {QStringLiteral("encryption_key"), keyB64},
              {QStringLiteral("encryption_iv"), ivB64}},
             [this](const QJsonObject& obj, bool, const QString& netErr) {
        const bool ok = obj.value(QStringLiteral("success")).toBool(false);
        emit storyCreated(ok, ok ? QString() : (netErr.isEmpty()
            ? QStringLiteral("Не удалось опубликовать историю")
            : netErr));
    });
}

void ApiClient::exportData() {
    // Ответ — JSON-файл целиком (может быть большой): читаем поток в QByteArray.
    QNetworkRequest req(QUrl(base_ + QStringLiteral("/api/export-data")));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    const QByteArray payload = QJsonDocument(
        QJsonObject{{QStringLiteral("token"), Session::instance().token}}).toJson(QJsonDocument::Compact);
    QNetworkReply* reply = nam_->post(req, payload);
    QObject::connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { emit dataExported(false, {}); return; }
        const QByteArray bytes = reply->readAll();
        emit dataExported(!bytes.isEmpty(), bytes);
    });
}

// ── Xipher Pulse: платёж и статус подписки (как pulse.js + chat.js веба) ─────

void ApiClient::premiumCreatePayment(const QString& plan, const QString& provider) {
    postJson(QStringLiteral("/api/premium/create-payment"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("plan"), plan},
              {QStringLiteral("provider"), provider}},
             [this](const QJsonObject& obj, bool ok, const QString& netErr) {
        const QJsonObject data = obj.value(QStringLiteral("data")).toObject();
        const bool success = ok && obj.value(QStringLiteral("success")).toBool(false);
        emit premiumPaymentReady(success, data, success
            ? QString()
            : (netErr.isEmpty()
               ? obj.value(QStringLiteral("message")).toString(QStringLiteral("Не удалось создать платёж"))
               : netErr));
    });
}

void ApiClient::premiumRefreshStatus() {
    // Эндпоинт как в refreshPremiumStatus веба: /api/validate-token отдаёт
    // is_premium / premium_plan / premium_expires_at в data.
    postJson(QStringLiteral("/api/validate-token"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool ok, const QString&) {
        const AuthResult r = ok ? parseAuth(obj) : AuthResult{};
        emit premiumStatusRefreshed(r.isPremium, r.premiumExpiresAt);
    });
}

// ── Боты: callback-кнопки и MiniApp (1:1 с handleKeyboardButtonClick веба) ───

void ApiClient::botCallback(const QString& messageId, const QString& callbackData) {
    postJson(QStringLiteral("/api/bot-callback-query"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("message_id"), messageId},
              {QStringLiteral("callback_data"), callbackData},
              {QStringLiteral("from_user_id"), Session::instance().userId}},
             [this](const QJsonObject& obj, bool ok, const QString&) {
        emit botCallbackDone(ok && obj.value(QStringLiteral("success")).toBool(false),
                             obj.value(QStringLiteral("response")).toObject());
    });
}

void ApiClient::botMiniappInit(const QString& botUserId) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token}};
    if (!botUserId.isEmpty()) body.insert(QStringLiteral("bot_user_id"), botUserId);
    postJson(QStringLiteral("/api/bot-miniapp-init"), body,
             [this](const QJsonObject& obj, bool ok, const QString&) {
        emit miniappInitReady(ok && obj.value(QStringLiteral("success")).toBool(false),
                              obj.value(QStringLiteral("init_data")).toString());
    });
}

// ── Подарки (как js/profile/gifts.js веба) ───────────────────────────────────

// Коллекция подарков человека — как openCollection в profile/gifts.js веба.
void ApiClient::giftsOfUser(const QString& userId) {
    postJson(QStringLiteral("/api/gifts/of-user"),
             {{QStringLiteral("token"), Session::instance().token},
              {QStringLiteral("user_id"), userId}},
             [this, userId](const QJsonObject& o, bool ok, const QString&) {
        const bool success = ok && o.value(QStringLiteral("success")).toBool(false);
        const bool hidden = success && o.value(QStringLiteral("hidden")).toBool(false);
        emit userGiftsLoaded(userId,
                             success ? o.value(QStringLiteral("gifts")).toArray()
                                     : QJsonArray(),
                             success, hidden);
    });
}

void ApiClient::giftsCatalog() {
    postJson(QStringLiteral("/api/gifts/catalog"),
             {{QStringLiteral("token"), Session::instance().token}},
             [this](const QJsonObject& obj, bool ok, const QString&) {
        emit giftsCatalogLoaded(obj.value(QStringLiteral("gifts")).toArray(
            obj.value(QStringLiteral("catalog")).toArray()));
    });
}

void ApiClient::giftSend(const QString& giftId, const QString& toUserId,
                         const QString& note, bool hideSender) {
    QJsonObject body{{QStringLiteral("token"), Session::instance().token},
                     {QStringLiteral("gift_id"), giftId},
                     {QStringLiteral("to_user_id"), toUserId},
                     {QStringLiteral("hide_sender"), hideSender}};
    if (!note.isEmpty()) body.insert(QStringLiteral("message_text"), note.left(200));
    postJson(QStringLiteral("/api/gifts/send"), body,
             [this](const QJsonObject& obj, bool ok, const QString&) {
        const bool s = ok && obj.value(QStringLiteral("success")).toBool(false);
        emit giftSent(s, s ? QString() : obj.value(QStringLiteral("message")).toString(
            obj.value(QStringLiteral("error")).toString(QStringLiteral("Не удалось отправить подарок"))));
    });
}
