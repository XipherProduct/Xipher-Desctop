#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QHash>
#include <QJsonObject>
#include <QDateTime>
#include <QJsonArray>
#include <functional>

#include "net/Models.h"

class QNetworkAccessManager;
class QNetworkReply;

// Результат запроса аутентификации (login / register / validate-token).
struct AuthResult {
    bool    success = false;
    QString message;
    QString token;
    QString userId;
    QString username;
    bool    isPremium = false;
    QString premiumPlan;         // trial | month | year
    QString premiumExpiresAt;    // ISO срок подписки
};

// ─────────────────────────────────────────────────────────────────────────────
//  ApiClient — обёртка над прод-API https://messenger.xipher.pro.
//  Эндпоинты повторяют web/js/login.js и web/js/register.js:
//    POST /api/login           {username,password} → {success,message,data:{...}}
//    POST /api/register        {username,password} → {success,message}
//    POST /api/check-username  {username}          → {success,message}
//    POST /api/validate-token  {token}             → {success,data:{...}}
// ─────────────────────────────────────────────────────────────────────────────
class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);

    void login(const QString& username, const QString& password);
    void registerUser(const QString& username, const QString& password);
    void checkUsername(const QString& username);
    void validateToken(const QString& token);

    // Чат (токен берётся из Session).
    void getChats();
    // limit>0 — размер страницы; beforeId — догрузка СТАРЫХ сообщений
    // (сервер: /api/messages?before_id, как пагинация в веб-клиенте).
    void getMessages(const QString& friendId, int limit = 50, const QString& beforeId = QString());
    void sendMessage(const QString& receiverId, const QString& content, const QString& tempId,
                     int ttlSeconds = 0, const QString& replyTo = QString());
    void deleteMessage(const QString& messageId, ChatKind kind, const QString& peerId);
    // Универсальная отправка с произвольным message_type (location/checklist/…).
    void sendRaw(const QString& receiverId, const QString& content,
                 const QString& messageType, const QString& tempId);

    // Голосовые / файлы.
    void uploadVoice(const QByteArray& audioBytes, const QString& mimeType, const QString& tempId);
    void sendVoice(const QString& receiverId, const QString& filePath, const QString& fileName,
                   long long fileSize, const QString& caption, const QString& tempId);
    void uploadFile(const QByteArray& bytes, const QString& fileName, const QString& tempId);
    void sendFile(const QString& receiverId, const QString& filePath, const QString& fileName,
                  long long fileSize, const QString& caption, const QString& tempId,
                  const QString& messageType = QStringLiteral("file"));
    void fetchFile(const QString& filePath);   // GET /files/... с токеном (стриминг)
    void cancelFetch(const QString& filePath); // прервать активную загрузку
    void fetchFileParallel(const QString& filePath, qint64 total, int chunks = 6); // чанки параллельно

    // Супер-поиск (как supersearch.js веба): /api/search-messages.
    void searchMessages(const QString& chatId, const QString& context,
                        const QString& query, const QString& type,
                        int offset = 0, int limit = 50);

    // Превью ссылок для композера: /api/link-preview.
    void fetchLinkPreview(const QString& url);

    // Экспорт данных: POST /api/export-data → JSON-файл.
    void exportData();

    // Xipher Pulse (подписка): создание платежа и обновление статуса.
    void premiumCreatePayment(const QString& plan, const QString& provider);
    void premiumRefreshStatus();
    int  folderCount() const { return folderCount_; }

    // Сторис (как stories.src.js веба): /api/stories/*.
    void loadStories();
    void storyView(const QString& storyId);
    void storyDelete(const QString& storyId);
    void storyCreate(const QString& mediaUrl, const QString& mediaType,
                     const QString& caption, const QString& privacy);

    // Звонки (сигналинг)
    void getTurnConfig();
    void callNotify(const QString& receiverId, const QString& callType);
    void callOffer(const QString& receiverId, const QString& sdp);
    void callAnswer(const QString& receiverId, const QString& sdp);
    void callIce(const QString& receiverId, const QString& candidate);
    void callEnd(const QString& receiverId);
    void getCallOffer(const QString& callerId);
    void getCallAnswer(const QString& calleeId);
    void getCallIce(const QString& otherId);
    void checkIncomingCalls();

    // Группы и каналы.
    void getGroups();
    void getChannels();
    void createGroup(const QString& name, const QString& description);
    void createChannel(const QString& name, const QString& description, const QString& customLink);
    void uploadChannelAvatar(const QString& channelId, const QByteArray& bytes, const QString& fileName);
    void getGroupMessages(const QString& groupId, int limit = 100, const QString& beforeId = QString());
    void getChannelMessages(const QString& channelId, int limit = 100, const QString& beforeId = QString());
    void sendGroupMessage(const QString& groupId, const QString& content, const QString& tempId,
                          const QString& replyTo = QString());
    void sendChannelMessage(const QString& channelId, const QString& content, const QString& tempId,
                            const QString& replyTo = QString());
    void publicDirectory(const QString& category, const QString& search, int offset);
    void joinPublic(const QString& id, const QString& type);   // type: "channel"|"group"

    // Управление каналом/группой.
    void getChannelInfo(const QString& channelId);
    void getMembers(const QString& peerId, bool isChannel);
    void updatePeerName(const QString& peerId, bool isChannel, const QString& name);
    void updatePeerDescription(const QString& peerId, bool isChannel, const QString& desc);
    void setPeerCustomLink(const QString& peerId, bool isChannel, const QString& link);
    void unsubscribeChannel(const QString& channelId);
    void leaveGroup(const QString& groupId);
    void deleteChannel(const QString& channelId);
    void deleteGroup(const QString& groupId);
    void createInvite(const QString& peerId, bool isChannel);
    void kickGroupMember(const QString& groupId, const QString& userId);
    void setGroupMemberRole(const QString& groupId, const QString& userId, const QString& role);
    void banChannelMember(const QString& channelId, const QString& userId, bool banned);
    void muteGroupMember(const QString& groupId, const QString& userId, bool muted);
    void setGroupPermission(const QString& groupId, const QString& permission, bool enabled);
    void pinMessage(const QString& messageId, ChatKind kind, const QString& peerId, bool pin);
    // Правка своего сообщения: {chat_type, message_id, content}; эхо — WS message_edited.
    void editMessage(const QString& messageId, const QString& content, ChatKind kind);

    // Форум-топики (группы).
    void getGroupTopics(const QString& groupId);
    void setGroupForumMode(const QString& groupId, bool enabled);
    void createGroupTopic(const QString& groupId, const QString& name,
                          const QString& emoji, const QString& color);
    void updateGroupTopic(const QString& topicId, const QString& name,
                          const QString& emoji, const QString& color, int closed /*-1 noop,0,1*/);
    void deleteGroupTopic(const QString& topicId);
    void getTopicMessages(const QString& topicId);
    void sendTopicMessage(const QString& topicId, const QString& content, const QString& tempId,
                          const QString& replyTo = QString());

    // Папки чатов (синхронизация с сервером).
    void getChatFolders();
    void setChatFolders(const QList<Folder>& folders);

    // ── Боты (1:1 с вебом) ────────────────────────────────────────────────────
    // Нажатие inline-кнопки с callback_data: /api/bot-callback-query.
    void botCallback(const QString& messageId, const QString& callbackData);
    // Подписанный контекст пользователя для MiniApp: /api/bot-miniapp-init.
    void botMiniappInit(const QString& botUserId);
    // Подарки: каталог, отправка и коллекция пользователя (profile/gifts.js).
    void giftsCatalog();
    void giftsOfUser(const QString& userId);
    void giftSend(const QString& giftId, const QString& toUserId,
                  const QString& note, bool hideSender);

    // Настройки: профиль, приватность, сеансы, email восстановления.
    void getMyProfile();
    void updateMyProfile(const QString& firstName, const QString& lastName, const QString& bio,
                         int birthDay, int birthMonth, int birthYear);
    void uploadAvatar(const QByteArray& bytes, const QString& fileName);
    void updateMyPrivacy(const QJsonObject& fields);
    void getSessions();
    void revokeSession(const QString& sessionId);
    void revokeSelectedSessions(const QStringList& ids);
    void revokeOtherSessions();
    void getRecoveryEmail();
    void setRecoveryEmail(const QString& email);

    // Люди и друзья.
    void getUserProfile(const QString& userId);
    // Реакции на сообщения (эхо состояния — WS reaction_update).
    void addMessageReaction(const QString& messageId, const QString& emoji,
                            const QString& messageContext = QStringLiteral("chat"));
    void removeMessageReaction(const QString& messageId, const QString& emoji,
                               const QString& messageContext = QStringLiteral("chat"));
    // Профиль v2 — как js/profile/view.js веба: /api/profile/view отдаёт
    // profile + relation + marks + gifts одним ответом, правила приватности
    // решает сервер. Ответ помечается id запроса — быстрое переключение
    // между людьми не должно нарисовать чужой профиль.
    void profileView(const QString& userId);
    // Счётчик общих медиа — POST /api/media/list { chat_type:'dm', chat_id }.
    void mediaCount(const QString& chatId);
    void callsMissedCount();
    void requestMediaList(const QString& chatId, const QString& category,
                          const QString& cursor = QString(), int limit = 60);
    void requestMediaCounts(const QString& chatId);
    void setContactName(const QString& contactId, const QString& customName);
    void searchUsers(const QString& query);
    void getFriends();
    void removeFriend(const QString& userId);
    void blockUser(const QString& userId);
    void unblockUser(const QString& userId);
    void deleteChat(const QString& chatId);
    void clearHistory(const QString& chatId, ChatKind kind);
    void sendFriendRequest(const QString& username);
    void getFriendRequests();
    void acceptFriend(const QString& requestId);
    void rejectFriend(const QString& requestId);

    QString baseUrl() const { return base_; }
    void setBaseUrl(const QString& url) { base_ = url; }
    // id последнего отправленного /api/profile/view (для отброса устаревших).
    qint64 profileViewReply() const { return profileViewReqId_; }

signals:
    void loginFinished(const AuthResult& result);
    void registerFinished(const AuthResult& result);
    void usernameChecked(const QString& username, bool available, const QString& message);
    void tokenValidated(const AuthResult& result);

    void chatsLoaded(const QList<Chat>& chats);
    void messagesLoaded(const QString& friendId, const QList<ChatMessage>& messages);
    void messageSent(const ChatMessage& message, const QString& receiverId, const QString& tempId);
    void messageDeleted(bool ok);
    void chatError(const QString& context, const QString& message);

    // voiceUploaded → отдаёт путь на сервере; затем зовём sendVoice.
    void voiceUploaded(const QString& filePath, const QString& fileName, long long fileSize, const QString& tempId);
    void fileUploaded(const QString& filePath, const QString& fileName, long long fileSize, const QString& tempId);
    void fileFetched(const QString& filePath, const QByteArray& bytes);
    void fileProgress(const QString& filePath, qint64 received, qint64 total);

    void usersFound(const QString& query, const QList<UserHit>& users);
    void friendsLoaded(const QList<UserHit>& friends);
    void contactActionDone(bool ok);
    void chatActionDone(bool ok);
    void profileLoaded(const QJsonObject& profile);
    void messageEditedOnServer(bool ok);
    void historyCleared(const QString& chatId, bool ok);
    // Профиль v2: полный ответ /api/profile/view + id запроса (для отмены
    // устаревшего) и флаг ошибки сети.
    void profileViewLoaded(qint64 reqId, const QJsonObject& data, bool ok, const QString& error);
    void mediaCountLoaded(const QString& chatId, int total);
    void callsMissedLoaded(int count);
    void mediaListLoaded(const QString& chatId, const QJsonArray& items);
    void mediaCountsLoaded(const QString& chatId, const QJsonObject& counts, int total);

    // Супер-поиск / превью ссылок / сторис.
    void messagesSearched(const QString& requestId, const QJsonArray& messages);
    void linkPreviewFetched(const QString& url, const QJsonObject& preview);
    void storiesLoaded(const QJsonObject& data);
    void storyCreated(bool ok, const QString& message);
    void storyDeleted(const QString& storyId, bool ok);
    void dataExported(bool ok, const QByteArray& bytes);
    void premiumPaymentReady(bool ok, const QJsonObject& data, const QString& message);
    void premiumStatusRefreshed(bool active, const QString& expiresAt);

    // Группы/каналы/каталог.
    void groupsLoaded(const QList<Chat>& groups);
    void channelsLoaded(const QList<Chat>& channels);
    void groupCreated(bool ok, const QString& message);
    void channelCreated(bool ok, const QString& message);
    void channelAvatarUploaded(bool ok);
    void groupMessagesLoaded(const QString& groupId, const QList<ChatMessage>& messages);
    void channelMessagesLoaded(const QString& channelId, const QList<ChatMessage>& messages);
    void directoryLoaded(const QList<DirectoryItem>& items, bool hasMore);
    void publicJoined(bool ok, const QString& id);
    void foldersLoaded(const QList<Folder>& folders);
    void foldersSaved(bool ok);
    void channelInfoLoaded(const QJsonObject& info);
    void membersLoaded(const QString& peerId, const QList<Member>& members);
    void peerActionDone(bool ok, const QString& message);
    void inviteLinkReady(const QString& url);
    void topicsLoaded(const QString& groupId, bool forumMode, const QList<Topic>& topics);
    void topicActionDone(bool ok, const QString& message);
    void topicMessagesLoaded(const QString& topicId, const QList<ChatMessage>& messages);

    // Настройки.
    void profileUpdated(bool ok, const QString& message);
    void avatarUploaded(bool ok, const QString& avatarUrl);
    void privacyUpdated(bool ok);
    void sessionsLoaded(const QList<SessionInfo>& sessions);
    void sessionsChanged();
    void recoveryEmailLoaded(const QString& email);
    void recoveryEmailSaved(bool ok, const QString& message);
    void contactRenamed(const QString& contactId, const QString& newName, bool ok);

    void turnConfigReady(const QList<IceServerCfg>& iceServers);
    void callOfferReady(const QString& callerId, const QString& sdp);
    void callAnswerReady(const QString& calleeId, const QString& sdp);
    void callIceBatch(const QString& otherId, const QStringList& candidates);
    void incomingCall(const QString& callerId, const QString& callerName, const QString& callType);
    void friendRequestSent(const QString& username, bool ok, const QString& message);
    void friendRequestsLoaded(const QList<FriendRequest>& requests);
    void friendActionDone(const QString& requestId, bool accepted, bool ok);

    // Боты и подарки.
    void botCallbackDone(bool ok, const QJsonObject& response);
    void miniappInitReady(bool ok, const QString& initData);
    void giftsCatalogLoaded(const QJsonArray& gifts);
    // Коллекция человека: /api/gifts/of-user. hidden=true — человек скрыл.
    void userGiftsLoaded(const QString& userId, const QJsonArray& gifts,
                         bool ok, bool hidden);
    void giftSent(bool ok, const QString& message);

public:
    // Доступ к сетевому менеджеру/базе — для загрузки медиа (сторис).
    QNetworkAccessManager* nam() const { return nam_; }

private:
    // Низкоуровневый POST JSON. callback(obj, ok, networkError).
    void postJson(const QString& path,
                  const QJsonObject& body,
                  std::function<void(const QJsonObject&, bool, const QString&)> callback);

    qint64 profileViewReqId_ = 0;   // монотонный id запроса профиля v2

    QHash<QString, QNetworkReply*> fetchReplies_;   // активные загрузки (path → reply)
    QHash<QString, QList<QNetworkReply*>> parallelReplies_;
    QHash<QString, qint64> failedFetches_;   // path → retry-not-before (ms epoch)
    int searchSeq_ = 0;   // последовательность запросов супер-поиска (сопоставление ответов)
    int folderCount_ = 0; // папок в последнем getChatFolders

    static AuthResult parseAuth(const QJsonObject& obj);

    // Fallback на /api/turn-config (как у веба), если turn-credentials недоступен.
    void getTurnConfigFallback();

    QNetworkAccessManager* nam_ = nullptr;
    QString base_ = QStringLiteral("https://messenger.xipher.pro");
};
