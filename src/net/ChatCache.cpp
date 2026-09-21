#include "net/ChatCache.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace {

constexpr int kMaxCached = 500; // хвост истории: для мгновенного открытия важен низ

QJsonObject toJson(const ChatMessage& m) {
    return QJsonObject{
        {QStringLiteral("id"),  m.id},
        {QStringLiteral("sid"), m.senderId},
        {QStringLiteral("sn"),  m.senderName},
        {QStringLiteral("c"),   m.content},
        {QStringLiteral("t"),   m.messageType},
        {QStringLiteral("tm"),  m.time},
        {QStringLiteral("ca"),  m.createdAt},
        {QStringLiteral("st"),  m.status},
        {QStringLiteral("out"), m.sent},
        {QStringLiteral("rd"),  m.isRead},
        {QStringLiteral("dl"),  m.isDelivered},
        {QStringLiteral("ttl"), m.ttlSeconds},
        {QStringLiteral("fp"),  m.filePath},
        {QStringLiteral("fn"),  m.fileName},
        {QStringLiteral("fs"),  m.fileSize},
        {QStringLiteral("ra"),  m.replyAuthor},
        {QStringLiteral("rs"),  m.replySnippet},
    };
}

ChatMessage fromJson(const QJsonObject& o) {
    ChatMessage m;
    m.id          = o.value(QStringLiteral("id")).toString();
    m.senderId    = o.value(QStringLiteral("sid")).toString();
    m.senderName  = o.value(QStringLiteral("sn")).toString();
    m.content     = o.value(QStringLiteral("c")).toString();
    m.messageType = o.value(QStringLiteral("t")).toString(QStringLiteral("text"));
    m.time        = o.value(QStringLiteral("tm")).toString();
    m.createdAt   = o.value(QStringLiteral("ca")).toString();
    m.status      = o.value(QStringLiteral("st")).toString();
    m.sent        = o.value(QStringLiteral("out")).toBool();
    m.isRead      = o.value(QStringLiteral("rd")).toBool();
    m.isDelivered = o.value(QStringLiteral("dl")).toBool();
    m.ttlSeconds  = o.value(QStringLiteral("ttl")).toInt();
    m.filePath    = o.value(QStringLiteral("fp")).toString();
    m.fileName    = o.value(QStringLiteral("fn")).toString();
    m.fileSize    = static_cast<long long>(o.value(QStringLiteral("fs")).toInteger());
    m.replyAuthor = o.value(QStringLiteral("ra")).toString();
    m.replySnippet= o.value(QStringLiteral("rs")).toString();
    return m;
}

} // namespace

ChatCache& ChatCache::instance() {
    static ChatCache c;
    return c;
}

QList<ChatMessage> ChatCache::load(const QString& peerId) {
    const QByteArray blob = store_.load(peerId);
    if (blob.isEmpty()) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(blob);
    if (!doc.isObject()) return {};
    QList<ChatMessage> msgs;
    for (const QJsonValue& v : doc.object().value(QStringLiteral("msgs")).toArray())
        msgs.append(fromJson(v.toObject()));
    return msgs;
}

void ChatCache::save(const QString& peerId, const QList<ChatMessage>& msgs) {
    // Для мгновенного открытия важен низ истории — храним хвост.
    QJsonArray arr;
    const int from = qMax(0, msgs.size() - kMaxCached);
    for (int i = from; i < msgs.size(); ++i) arr.append(toJson(msgs[i]));
    store_.save(peerId, QJsonDocument(
        QJsonObject{{QStringLiteral("v"), 1}, {QStringLiteral("msgs"), arr}}
    ).toJson(QJsonDocument::Compact));
}

void ChatCache::clearAll() {
    store_.clearAll();
}
