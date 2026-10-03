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
class QSplitter;
class QFrame;
class QPropertyAnimation;
class QGraphicsOpacityEffect;
#include "ui/Stories.h"   // StoryUserGroup (QList<> требует полный тип)

class StoriesBar;
class StoriesViewer;
class StoryCreatorDialog;
class SuperSearchDialog;
class LinkPreviewBar;
class QuickSwitcher;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QPropertyAnimation;

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

    void openQuickSwitcher();   // Ctrl+K — быстрый переход (DSC-01; публично для теста)

    // Esc-каскад (KEY-01): закрыть верхний оверлей; true = что-то было открыто.
    // Порядок: модалки → шторка меню → эмодзи → третья колонка → поиск в сайдбаре.
    bool consumeEscape();
    // Полная пересылка с аттачами (MSG-02; публично для design-verify).
    void forwardMessageFull(const ChatMessage& msg);

    // Тестовые швы навигации (design-verify): закрытые действия по имени.
    void debugAction(const QString& name, int arg = 0);
    QString debugCurrentChatId() const { return currentPeerId_; }
    int debugChatIndex(const QString& id) const { return indexOfChat(id); }
    int debugMessageCount() const { return currentMessages_.size(); }   // MLT-02 тест
    int debugCurrentQueueIdx() const { return audioIdx_; }               // VOX-03 тест
    // Прыжок из поиска с подсветкой (SRC-04; публично для design-verify).
    void onSearchResultPickedForTest(const QString& chatId, const QString& messageId,
                                     const QString& keywords) {
        onSearchResultPicked(chatId, messageId, keywords);
    }
    // Обновление карточки опроса тестом (MSG-06).
    void onPollLoadedForTest(const QString& mid, const QJsonObject& poll, bool ok) {
        onPollLoaded(mid, poll, ok);
    }

    // Третья колонка (WIN-01/02): инфо о чате справа. 0/360/фулл, анимация
    // 200мс; при узком окне (<1000px) — оверлей поверх чата со скримом.
    void setThirdColumnOpen(bool open, bool full = false);
    void toggleThirdColumn();
    void closeThirdColumn();
    bool isThirdColumnOpen() const { return thirdColTarget_ > 0; }
    void updateThirdColumnMode();   // сплиттер ↔ оверлей по ширине окна
    void setThirdColumnInfo();      // данные текущего чата в колонку
    void animateThirdColumnTo(int targetW);   // 200мс ease-out (WIN-02)
    void fadeScrim(bool on);        // затемнение чата в оверлей-режиме
    int  sidebarWidth() const;
    // Клавиатурная навигация (KEY-02..04).
    void cycleChat(int delta);           // Ctrl+PgUp/PgDn по видимому списку
    void navigateChatHistory(int delta); // Alt+←/→ по стекам переходов
    void editLastOwnMessage();           // Ctrl+↑ — правка последнего своего

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
    // Drag-n-drop файлов в чат (MLT-06): дроп anywhere → очередь аттачей.
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

signals:
    void logoutRequested();
    void callRequested(const QString& peerId, const QString& peerName, const QString& avatarUrl);
    void notify(const QString& title, const QString& body);   // системное уведомление
    void pollBarsNeedUpdate();   // опрос: пересчитать полосы после layout-прохода

public:
    void openChatWith(const QString& userId, const QString& displayName, const QString& username);

    // Тестовый шов (offscreen-верификация дизайна, tests/design-verify.cpp):
    // подсадить чаты/папки/историю без сети и открыть чат.
    void injectForDesignTest(const QList<Chat>& chats, const QList<Folder>& folders,
                             const QString& openChatId, const QList<ChatMessage>& messages,
                             const QStringList& pinnedKeys = QStringList());
    // Тестовый шов для дропа (MLT-06): Qt 6.11 глотает синтетические
    // QDropEvent в sendEvent, поэтому стейджинг дергаем напрямую.
    void injectDroppedUrlsForTest(const QList<QUrl>& urls) { stageFiles(urls); }
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
    void onVoiceRecorded(const QString& filePath, const QString& mimeType,
                         const QByteArray& pcmDup, int pcmDurationMs);
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
    void onSearchResultPicked(const QString& chatId, const QString& messageId,
                              const QString& keywords);
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
    // Пины чатов (LST-04): оптимистично + подтверждение сервера; лимит 3/10.
    void setChatPinned(const Chat& c, bool pinned);
    // Drag-n-drop аттачи (MLT-06): очередь перед отправкой + превью над композером.
    void stageFiles(const QList<QUrl>& urls);     // добавить в очередь (валидация)
    void renderStagedBar();                        // пересобрать полосу превью
    void clearStagedFiles();
    void setDropOverlayActive(bool on);            // «Отпустите, чтобы прикрепить»
    // Отправка локального файла (диалог, дроп, стейджинг — один путь):
    // оптимистичный баббл + upload; asImage → превью сразу.
    void sendLocalFile(const QString& path, bool asImage);
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

    // Drag-n-drop аттачи (MLT-06): стейджинг перед отправкой, как
    // pendingAttachments в вебе — превью-чипы над композером, отправка вместе
    // с текстом (текст уходит первым сообщением, файлы следом).
    struct StagedAttachment {
        QString path;
        QString name;
        qint64  size = 0;
        bool    isImage = false;
    };
    QList<StagedAttachment> stagedFiles_;
    QWidget* stagedBar_    = nullptr;   // полоса превью над композером
    QVBoxLayout* stagedLay_ = nullptr;  // ряд чипов внутри полосы
    QWidget* dropOverlay_  = nullptr;   // подсветка «Отпустите, чтобы прикрепить»

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
    // Закреплённые чаты (LST-04): ключи «type:id» из /api/get-chat-pins,
    // показываются секцией сверху списка.
    QSet<QString> pinnedChats_;
    // Архив (LST-03): локальные ключи «type:id» (Prefs xipher_archived_chats),
    // секция «Архив (N)» внизу списка, клик по заголовку разворачивает.
    QSet<QString> archivedChats_;
    bool          archiveOpen_ = false;
    void loadArchivedChats();
    void saveArchivedChats();
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
    // Третья колонка (WIN-01/02): QSplitter [сайдбар|чат|инфо], ширина 0/360/
    // фулл; <1000px — оверлей поверх чата (thirdColOverlayMode_), под ним
    // скрим с фейдом (затемнение чата). Анимация — QVariantAnimation по ширине.
    QSplitter*           splitter_ = nullptr;
    QFrame*              thirdCol_ = nullptr;
    QVBoxLayout*         thirdColLay_ = nullptr;
    QLabel*              tcAvatar_ = nullptr;
    QLabel*              tcName_   = nullptr;
    QLabel*              tcSub_    = nullptr;
    QPushButton*         tcExpandBtn_ = nullptr;
    QPushButton*         tcManageBtn_ = nullptr;   // «Управление» (группы/каналы)
    QWidget*             tcMeta_  = nullptr;      // строки-мета (тип/участники/ссылка)
    QWidget*             overlayScrim_ = nullptr; // затемнение чата в overlay-режиме
    QGraphicsOpacityEffect* scrimFx_ = nullptr;
    QVariantAnimation*   thirdColAnim_ = nullptr;
    int                  thirdColTarget_ = 0;     // 0 = закрыто; 360; <full>
    bool                 thirdColFull_ = false;   // режим «фулл» (45% окна)
    bool                 thirdColOverlayMode_ = false;
    StoriesBar*          storiesBar_ = nullptr;
    QList<StoryUserGroup> storyGroups_;
    StoriesViewer*       storiesViewer_ = nullptr;
    StoryCreatorDialog*  storyCreator_ = nullptr;
    SuperSearchDialog*   superSearch_ = nullptr;
    LinkPreviewBar*      linkPreview_ = nullptr;
    QuickSwitcher*       quickSwitcher_ = nullptr;   // Ctrl+K (DSC-01)
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
    qreal voiceRate_ = 1.0;   // скорость голосовых (VOX-02), живёт между треками
    // Мини-плеер с очередью (VOX-03): все аудио чата, next/prev/shuffle.
    void buildAudioQueue();
    void playQueueAt(int idx);
    void queueNext();
    void queuePrev();
    void toggleShuffle();
    void updatePlayerBar();
    QWidget* audioBar_ = nullptr;
    QLabel* audioTitle_ = nullptr;
    QPushButton* audioPlayBtn_ = nullptr;
    QPushButton* audioShuffleBtn_ = nullptr;
    QStringList audioQueue_, audioNames_;
    int audioIdx_ = -1;
    bool audioShuffle_ = false;

    // Обрезка голосовых (VOX-01): трим-диапазон перед отправкой.
    void openVoiceTrimDialog(const QString& m4aPath, const QByteArray& pcm, int durMs);
    void sendVoiceFile(const QString& path, const QString& mimeType, int secs);

    // Подсветка поискового запроса в бабблах (SRC-04): живёт с последнего
    // прыжка из поиска до смены чата.
    QString highlightQuery_;

    // Опросы (MSG-06): живые карточки по message_id и черновики создания
    // (pendingPolls_: temp_id → параметры, отправляются create-poll на ack).
    QHash<QString, QPair<QWidget*, QLabel*>> pollWidgets_;
    struct PollDraft { QString question; QStringList options; bool anonymous = true; bool multiple = false; };
    QHash<QString, PollDraft> pendingPolls_;
    // Отложенные (MSG-07): секция над композером + recurrence-планировщик
    // (сверяет scheduled_id раз в минуту; исчез → отправлено → следующее).
    QWidget*      scheduledBar_ = nullptr;
    QVBoxLayout*  scheduledLay_ = nullptr;
    QTimer*       scheduledCheckTimer_ = nullptr;
    QList<QJsonObject> checkRecurringQueue_;

    // Мультивыбор сообщений (MLT-01/02): рамка на баббле, панель действий
    // над композером, Esc выходит. Выделяются только материализованные.
    bool         selectionMode_ = false;
    QSet<QString> selectedIds_;
    QWidget*     selectionBar_  = nullptr;
    QLabel*      selectionCount_ = nullptr;
    void enterSelectionMode(const QString& firstId = QString());
    void exitSelectionMode();
    void toggleSelected(const QString& id);
    void renderSelectionBar();
    QFrame* bubbleForId(const QString& id) const;
    void applySelectionVisual(QFrame* b, bool on);
    void deleteSelected();     // MLT-02
    void deleteSelectedConfirmed();   // без диалога (тесты)
    void forwardSelected();    // MLT-02
    void copySelected();       // MLT-02
    // Опросы (MSG-06): рендер карточки, обновление по get-poll, создание.
    void addPollBubble(QWidget* bubble, QVBoxLayout* bl, const ChatMessage& msg);
    void onPollLoaded(const QString& messageId, const QJsonObject& poll, bool ok);
    void openPollDialog();
    // Отложенные (MSG-07): диалог, секция над композером, recurrence-планировщик.
    void openScheduleDialog();
    void onScheduledLoaded(const QString& chatId, const QJsonArray& scheduled);
    void onScheduledCreated(bool ok, const QString& id, const QDateTime& sendAt,
                            const QString& error);
    void refreshScheduled();
    void checkRecurring();
    void checkRecurringForChat(const QString& chatType, const QString& chatId,
                               const QJsonArray& scheduled);
    void addRecurringDraft(const QString& chatType, const QString& chatId,
                           const QString& content, int intervalDays);
    // Streamer Mode (DSC-03).
    void applyStreamerMode();
    void updateStreamerLabel(QPushButton* btn);

    // Клавиатурная навигация (KEY-02/03): порядок видимого списка чатов
    // (обновляется в rebuildChatList) и стеки истории переходов.
    QList<QString> visibleChatIds_;
    QStringList    navBack_;
    QStringList    navForward_;
    bool           navHistoryNavigating_ = false;   // Alt-переход не пишет в стек
};
