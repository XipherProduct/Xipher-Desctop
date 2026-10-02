#pragma once
#include <QWidget>
#include <QList>
#include <QSet>
#include <QHash>
#include <QPointer>
#include <QJsonObject>

#include "net/Models.h"

class ApiClient;
class WsClient;
class VoiceRecorder;
class VoiceMessageWidget;
class RecordingBar;
class EmojiPicker;
class ChecklistWidget;
class EmptyChatGreeting;
class ProfilePanel;
class SettingsDialog;
class ComposerEdit;
class QNetworkAccessManager;
class QListWidget;
class QLineEdit;
class QPushButton;
class QLabel;
class QStackedWidget;
class QScrollArea;
class QVBoxLayout;
class QHBoxLayout;
class QTimer;
class QVariantAnimation;
class QMediaPlayer;
class QAudioOutput;
#include "ui/Stories.h"   // StoryUserGroup (QList<> требует полный тип)

class StoriesBar;
class StoriesViewer;
class StoryCreatorDialog;
class SuperSearchDialog;
class LinkPreviewBar;

// ─────────────────────────────────────────────────────────────────────────────
//  ChatPage — основной экран мессенджера (раскладка как в Telegram/веб-чате):
//  слева список чатов, справа переписка с полем ввода. Реальные данные с
//  прод-API + realtime по WebSocket.
// ─────────────────────────────────────────────────────────────────────────────
class ChatPage : public QWidget {
    Q_OBJECT
public:
    ChatPage(ApiClient* api, WsClient* ws, QWidget* parent = nullptr);

    void load();   // вызвать после входа: грузит чаты и запускает realtime

    void loadStoriesUi();   // сторис-бар (как в вебе)

    void applyTheme();   // перегенерация QSS при смене темы («Оформление»)
    void clampBubbleWidths();   // ≤480px и ≤72% ширины области сообщений
    void prependOlderMessages();   // догрузка старых при прокрутке к верху (как в ТГ)
    void prependOlderBatch(int floorFrom, int batch);   // порция prepend без якоря
    void continueTail(int targetFrom, quint64 gen);   // достройка хвоста чанками (tdesktop-style)
    void fetchOlderFromServer();   // серверная пагинация: /api/messages?before_id
    void smoothScrollTo(int target);   // плавная прокрутка (как в Telegram)
    void updateScrollDownButton();

protected:
    void resizeEvent(QResizeEvent* e) override;  // адаптация под узкое окно
    void keyPressEvent(QKeyEvent* e) override;   // Ctrl+Shift+F → супер-поиск

signals:
    void logoutRequested();
    void callRequested(const QString& peerId, const QString& peerName, const QString& avatarUrl);
    void notify(const QString& title, const QString& body);   // системное уведомление

public:
    void openChatWith(const QString& userId, const QString& displayName, const QString& username);

    // Тестовый шов (offscreen-верификация дизайна, tests/design-verify.cpp):
    // подсадить чаты/папки/историю без сети и открыть чат.
    void injectForDesignTest(const QList<Chat>& chats, const QList<Folder>& folders,
                             const QString& openChatId, const QList<ChatMessage>& messages);
    // Закреп: панель над списком (публично — тест и WS-обработчик).
    void setPinnedMessage(const QString& id, const QString& snippet);
    void clearPinnedMessage();

private slots:
    void openNewChatDialog();
    void openSettings();
    void toggleAppMenu();
    void closeAppMenu();
    void buildAppMenu();
    void onChatsLoaded(const QList<Chat>& chats);
    void onMessagesLoaded(const QString& friendId, const QList<ChatMessage>& messages);
    void onMessageSent(const ChatMessage& msg, const QString& receiverId, const QString& tempId);
    void onWsMessage(const QString& peerId, const ChatMessage& msg, const QString& tempId);
    void onChatClicked();
    void onSendClicked();
    void onSearchChanged(const QString& text);
    // Голосовые
    void onMicClicked();
    void cancelRecording();
    void stopAndSendVoice();
    void onVoiceRecorded(const QString& filePath, const QString& mimeType);
    void onVoiceUploaded(const QString& filePath, const QString& fileName, long long fileSize, const QString& tempId);
    void onFileFetched(const QString& filePath, const QByteArray& bytes);

    // Эмодзи / вложения / таймер
    void onEmojiClicked();
    void onAttachClicked();
    void onTimerClicked();
    void pickAndSendFile();
    void pickAndSendPhoto();
    void sendPhotoBytes(const QByteArray& bytes, const QString& fileName);
    void openChecklistDialog();
    void sendLocation();
    void startCall();
    void showChatMenu();
    void openRenameDialog();
    void onFileUploaded(const QString& filePath, const QString& fileName, long long fileSize, const QString& tempId);
    void onSearchResultPicked(const QString& chatId, const QString& messageId);
    void onBotCallbackDone(bool ok, const QJsonObject& response);
    void onMiniappInitReady(bool ok, const QString& initData);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void buildUi();
    void playVoice(const QString& serverPath);
    void onVoicePlayPause(VoiceMessageWidget* w, const QString& path);
    void rebuildChatList();
    void mergeAllChats();   // объединить личку + группы + каналы → chats_
    void rebuildFolderStrip();
    void setActiveFolder(const QString& id);
    void openFolderEditor(const QString& folderId);   // пустой id = новая папка
    void showTopicsList(const QList<Topic>& topics);
    void openTopic(const Topic& topic);
    void createTopicDialog();
    int  indexOfChat(const QString& id) const;
    void openChat(const Chat& chat);
    void addBubble(const ChatMessage& msg, bool prepend = false, bool animate = true);
    void showMessageMenu(QWidget* bubble, const QPoint& pos);
    void forwardMessage(const QString& text);
    void saveToSaved(const QString& text);   // «В избранное»: переслать в «Избранные»
    void setReplyTo(const QString& id, const QString& author, const QString& text);
    void clearReplyTo();
    void reloadCurrentMessages();
    void clearMessages();
    void applyMessages(const QList<ChatMessage>& messages, bool updateCache = true);
    void renderMessages(const QString& filter);   // отрисовать (с фильтром поиска)
    void updateGreeting();   // показать/скрыть пустой-чат приветствие
    void scrollToBottom();
    void tryJumpToPending();   // прыжок к сообщению из поиска, когда виджет достроен
    // Боты: inline-кнопки в бабблах, reply-клавиатура над композером, MiniApps.
    void addInlineKeyboard(QVBoxLayout* bubbleLayout, const ChatMessage& msg);
    void applyReplyKeyboard(const QJsonObject& markup);
    void hideBotKeyboard();
    void openMiniapp(const QString& url, const QString& botId);
    void openCatalog();   // каталог публичных каналов и групп
    // Локальный зашифрованный кэш истории (ChatCache): мгновенное открытие чата.
    QString cacheKey() const;          // обычный чат — peerId, тема форума — «t:<id>»
    void renderCached();               // показать кэш до ответа сервера
    void cacheCurrent();               // сохранить текущую историю в кэш
    void setBubbleImage(QLabel* img, const QByteArray& bytes);       // фото (кэш/сеть) → баббл
    void showMediaMenu(QWidget* src, const QString& filePath,
                       const QString& kind, const QPoint& pos);      // фото/ГС: сохранить/копировать/переслать
    QByteArray mediaBytes(const QString& filePath) const;            // кэш → локальный файл → пусто
    void sendPhotoBytesTo(const QString& receiverId, const QByteArray& bytes,
                          const QString& fileName);                  // пересылка фото
    void playVoiceBytes(const QString& serverPath, const QByteArray& bytes);  // голос → temp + play
    void openSuperSearch();                                          // Ctrl+Shift+F
    void jumpToMessage(const QString& messageId);                    // из результатов поиска
    void bumpChat(const QString& peerId, const QString& lastText, const QString& time, bool incrementUnread);
    // Реакции: чипы под бабблом, точечное обновление, тумбл.
    void addReactionChips(QWidget* bubble, QVBoxLayout* bl, const ChatMessage& msg);
    void refreshReactionChips(const QString& messageId);
    void toggleReaction(const QString& messageId, const QString& emoji);
    ChatMessage* findMessage(const QString& id);
    // Правка своего сообщения: вход в режим, отправка, выход.
    void startEditing(const QString& id, const QString& text);
    void cancelEditing();
    void rerenderPreservingScroll();
    void showTyping(const QString& chatId, bool on);
    void saveDraft();
    void restoreDraft();
    // Создаёт ProfilePanel (один экземпляр) и вешает ВСЕ связи, включая
    // «Избранное»/настройки/канал из профиля — обе точки открытия общие.
    void ensureProfilePanel();

    ApiClient* api_;
    WsClient*  ws_;

    // Сайдбар
    QListWidget* chatList_ = nullptr;
    QLineEdit*   search_   = nullptr;

    // Переписка
    QStackedWidget* convStack_ = nullptr;   // 0 — пусто, 1 — диалог
    QWidget*     peerHeader_ = nullptr;
    QPushButton* moreBtn_    = nullptr;
    QList<ChatMessage> currentMessages_;   // загруженные сообщения текущего чата
    QLabel*      peerName_   = nullptr;
    QLabel*      peerStatus_ = nullptr;
    QString      peerStatusBase_;             // обычный статус (typing его подменяет)
    QTimer*      typingTimer_ = nullptr;      // гашение «печатает…» через 4 с
    QString      draftKey_;                   // Prefs-ключ черновика текущего чата
    bool         typingSent_ = false;         // троттл своего статуса (5 с)
    QLabel*      peerAvatar_ = nullptr;
    ProfilePanel* profilePanel_ = nullptr;
    SettingsDialog* settings_ = nullptr;
    QPushButton* menuBtn_ = nullptr;
    QWidget*     appMenu_ = nullptr;       // шторка меню (как .app-menu веба)
    QWidget*     appMenuScrim_ = nullptr;
    QScrollArea* msgScroll_  = nullptr;
    QWidget*     msgContainer_ = nullptr;
    QVBoxLayout* msgLayout_  = nullptr;
    // Якорь низа (как в Telegram): пока true, любой пересчёт раскладки
    // (wordwrap-пасы, догрузка картинок, новые сообщения) держит вид снизу.
    // Отпускается, когда пользователь сам уходит от низа.
    bool         stickBottom_ = true;
    // История грузится (кэш пуст, ответ сервера не пришёл): приветствие
    // «Здесь пока ничего нет» не показываем — иначе оно мелькает.
    bool         loadingChat_ = false;
    // Серверная пагинация истории (before_id): есть ли ещё старые сообщения
    // на сервере и идёт ли сейчас их запрос.
    bool         hasMoreServer_ = true;
    bool         fetchingOlder_ = false;
    // Прыжок к сообщению из единого поиска: ждём достройку виджетов.
    QString      pendingJumpId_;
    QString      pendingMiniappUrl_;   // MiniApp ждёт initData от сервера
    EmptyChatGreeting* greeting_ = nullptr;
    int          bubbleCount_ = 0;   // сколько сообщений сейчас в переписке
    int          renderedFrom_ = 0;  // индекс первого ОТРИСОВАННОГО сообщения (хвост-рендер)
    bool         loadingOlder_ = false;
    quint64      renderGen_ = 0;    // поколение рендера: смена чата отменяет отложенные чанки
    bool         initialRenderPhase_ = false;
    bool         programmaticScroll_ = false;   // служебная прокрутка (не пользователь)
    QStackedWidget* composerStack_ = nullptr;  // 0 — ввод, 1 — запись
    QWidget*      composerBar_ = nullptr;      // для привязки эмодзи-панели справа
    QWidget*      botKeyboardBar_ = nullptr;   // reply-клавиатура бота (над вводом)
    QVBoxLayout*  botKeyboardLayout_ = nullptr;
    QJsonObject   currentReplyKeyboard_;       // активная reply-клавиатура бота
    ComposerEdit* composer_  = nullptr;
    QPushButton* sendBtn_    = nullptr;
    QPushButton* micBtn_     = nullptr;
    QPushButton* emojiBtn_   = nullptr;
    QPushButton* attachBtn_  = nullptr;
    QPushButton* timerBtn_   = nullptr;
    RecordingBar* recBar_    = nullptr;
    EmojiPicker*  emojiPicker_ = nullptr;
    int           disappearTtl_ = 0;   // сек, 0 = выкл (пока UI-состояние)
    QWidget*      replyBar_     = nullptr;   // полоса «ответ на…» над вводом
    QString       editingId_;                 // режим правки своего сообщения
    QWidget*      pinnedBar_    = nullptr;    // полоса закреплённого сообщения
    QLabel*       pinnedText_   = nullptr;
    QString       pinnedMsgId_;
    QLabel*       replyBarText_ = nullptr;
    QString       replyToId_, replyToName_, replyToText_;
    QTimer*       peerReloadTimer_ = nullptr;
    QTimer*       chatListDebounce_ = nullptr;   // пересборка списка чатов пачкой

    // Файлы: отложенная отправка/открытие
    QString pendingFileReceiver_;
    QSet<QString> pendingPhotoIds_;   // temp_id'ы, которые надо отправить как image
    QHash<QString, QString> pendingFileOpen_;   // серверный путь → имя для сохранения

    // Чек-листы (live-обновления) + гео
    QHash<QString, ChecklistWidget*> checklistWidgets_;   // id → виджет
    QHash<QString, QJsonObject>      mergedChecklists_;   // id → склеенное состояние
    QString                          pendingLocReceiver_;
    QNetworkAccessManager*           geoNam_ = nullptr;

    // Голос
    VoiceRecorder* recorder_ = nullptr;
    QMediaPlayer*  player_   = nullptr;
    QAudioOutput*  audioOut_ = nullptr;
    QString        pendingVoiceTempId_;
    QString        pendingVoiceReceiver_;
    int            pendingVoiceSecs_ = 0;
    QString        pendingPlayPath_;
    QHash<QString, QString> voiceCache_;   // серверный путь → локальный temp-файл

    // Текущий проигрываемый голосовой виджет (для прогресса/таймера).
    VoiceMessageWidget* activeVoice_ = nullptr;
    QString             activeVoicePath_;

    QList<Chat> chats_;            // объединённый список (личка + группы + каналы)
    QList<Chat> personalChats_;    // /api/chats
    QList<Chat> groupChats_;       // /api/get-groups
    QList<Chat> channelChats_;     // /api/get-channels
    ChatKind    currentKind_ = ChatKind::User;   // тип открытого чата
    bool        currentForum_ = false;           // открытая группа в режиме форума
    bool        currentCanManage_ = false;       // я создатель/админ открытой группы/канала
    QString     currentTopicId_;                 // открытая тема (пусто = не в теме)
    QPushButton* topicBackBtn_ = nullptr;
    QLabel*      topicsTitle_  = nullptr;
    QVBoxLayout* topicsBox_    = nullptr;
    QList<Topic> currentTopics_;
    QList<Folder> folders_;        // папки (синхр. с сервером)
    QString       activeFolderId_ = QStringLiteral("all");
    QWidget*      folderRail_ = nullptr;        // вертикальный рейл папок (72px, как в вебе)
    QScrollArea*  folderRailScroll_ = nullptr;
    QVBoxLayout*  folderRailItems_  = nullptr;

    // Функционал веб-клиента: сторис, супер-поиск, превью ссылок.
    QPushButton*         scrollDownBtn_ = nullptr;  // кнопка «вниз» (плавает справа)
    QVariantAnimation*   scrollAnim_ = nullptr;
    QWidget*             sidebar_ = nullptr;       // сжимается 380→300 при узком окне
    QWidget*             emptyPage_  = nullptr;   // тематизируемые страницы
    QWidget*             topicsPage_ = nullptr;
    StoriesBar*          storiesBar_ = nullptr;
    QList<StoryUserGroup> storyGroups_;
    StoriesViewer*       storiesViewer_ = nullptr;
    StoryCreatorDialog*  storyCreator_ = nullptr;
    SuperSearchDialog*   superSearch_ = nullptr;
    LinkPreviewBar*      linkPreview_ = nullptr;
    QList<UserHit> searchHits_;   // глобальный поиск людей в сайдбаре
    QList<DirectoryItem> directoryHits_;   // глобальный поиск: публичные каналы/группы
    QString     searchQuery_;
    QString     pendingJoinId_;   // публичный чат, к которому присоединяемся
    QTimer*     searchTimer_ = nullptr;
    QHash<QString, QPointer<QLabel>> pendingImage_;   // путь → QLabel для картинки
    QString     currentPeerId_;
    QString     currentPeerName_;
    QSet<QString> shownIds_;   // дедуп сообщений в открытом чате
    int tempCounter_ = 0;
};
