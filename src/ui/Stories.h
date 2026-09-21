#pragma once
#include <QWidget>
#include <QPointer>
#include <QList>

class QLabel;
class QHBoxLayout;
class QVBoxLayout;
class QScrollArea;
class QGridLayout;
class QLineEdit;
class QPushButton;
class QComboBox;
class ApiClient;
class QTimer;
class QEvent;
class QKeyEvent;
class QMouseEvent;

// ─────────────────────────────────────────────────────────────────────────────
//  Stories — сторис (1:1 со stories.src.js веб-клиента):
//    StoriesBar     — горизонтальная полоса колец в сайдбаре;
//    StoriesViewer  — полноэкранный просмотр (прогресс, навигация, реакции,
//                     ответ, удаление своих); медиа расшифровывается
//                     AES-256-GCM (ключ/IV приходят с историей с сервера);
//    StoryCreatorDialog — публикация: файл → /api/upload-file → AES-GCM
//                     → /api/stories/create.
// ─────────────────────────────────────────────────────────────────────────────
struct StoryItem {
    QString id;
    QString userId;
    QString username;
    QString avatarUrl;
    QString mediaUrl;
    QString mediaType;     // "image" | "video"
    QString caption;
    QString createdAt;
    bool    isViewed = false;
    bool    isOwn = false;
    QString keyB64;        // encryption_key из /api/stories/all
    QString ivB64;         // encryption_iv
};

struct StoryUserGroup {
    QString userId;
    QString username;
    QString avatarUrl;
    bool    hasUnread = false;
    bool    isOwn = false;
    QList<StoryItem> stories;
};

// ── Полоса сторис в сайдбаре ─────────────────────────────────────────────────
class StoriesBar : public QWidget {
    Q_OBJECT
public:
    explicit StoriesBar(QWidget* parent);
    void applyData(const QList<StoryUserGroup>& groups);
    void clear();

signals:
    void userClicked(int groupIndex);
    void addRequested();   // клик по «+» своей плитки

private:
    QWidget* makeTile(const StoryUserGroup& g, int index);
};

// ── Полноэкранный просмотрщик ────────────────────────────────────────────────
class StoriesViewer : public QWidget {
    Q_OBJECT
public:
    explicit StoriesViewer(ApiClient* api, QWidget* parent);
    void open(const QList<StoryUserGroup>& groups, int userIndex, int storyIndex = 0);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;

private slots:
    void showCurrent();
    void nextStory();
    void prevStory();

private:
    void buildUi();
    void markViewed(const StoryItem& s);
    void reactOrReply(bool react);

    ApiClient* api_;
    QList<StoryUserGroup> groups_;
    int userIdx_ = 0, storyIdx_ = 0;
    QWidget*   card_ = nullptr;
    QLabel*    media_ = nullptr;
    QLabel*    caption_ = nullptr;
    QLabel*    meta_ = nullptr;
    QWidget*   progressRow_ = nullptr;
    QList<QWidget*> progressBars_;
    QLineEdit* replyEdit_ = nullptr;
};

// ── Диалог создания ──────────────────────────────────────────────────────────
class StoryCreatorDialog : public QWidget {
    Q_OBJECT
public:
    StoryCreatorDialog(ApiClient* api, QWidget* parent);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void pickFile();
    void publish();

private:
    void buildUi();

    ApiClient* api_;
    QWidget*   card_ = nullptr;
    QLabel*    preview_ = nullptr;
    QByteArray mediaBytes_;
    QString    mediaName_, mediaType_;
    QLineEdit* captionEdit_ = nullptr;
    QComboBox* privacy_ = nullptr;
    QPushButton* postBtn_ = nullptr;
};

// Общая загрузка списка (использует ChatPage):
QList<StoryUserGroup> groupStoriesPayload(const QJsonObject& data, const QString& myId,
                                          bool* isPremiumOut);
