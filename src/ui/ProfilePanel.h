#pragma once
#include <QLabel>
#include <QJsonObject>
#include <QVariantAnimation>
#include "ui/Icons.h"
#include "ui/ModalOverlay.h"

class ApiClient;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QScrollArea;
class QStackedLayout;
class QFrame;

// ─────────────────────────────────────────────────────────────────────────────
//  Каркас на время загрузки — порт .xp-skel из css/profile.css веба.
//  Не спиннер: плашки сразу показывают будущую форму профиля, и содержимое
//  не прыгает, когда данные приходят. По плашкам бежит световая полоса
//  (shimmer, 1.4с) — перекрашивается только этот виджет.
// ─────────────────────────────────────────────────────────────────────────────
class ProfileSkeleton : public QWidget {
    Q_OBJECT
public:
    explicit ProfileSkeleton(QWidget* parent = nullptr);
    ~ProfileSkeleton() override;
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QVariantAnimation* shimmer_;
};

// ─────────────────────────────────────────────────────────────────────────────
//  ProfilePanel — профиль 1:1 с js/profile/view.js + css/profile.css веба:
//    • окно 560px по центру, фон #131218, радиус 24;
//    • скелетон на время запроса, затем шапка из списка чатов (если известна),
//      затем полный ответ /api/profile/view;
//    • баннер 186px (градиент бренда / personal_color / картинка) с затуханием
//      вниз и аватар 96px, ложащийся на него ровно наполовину;
//    • имя + знак, @username, «в сети»/«был(а) …», статус-настроение;
//    • действия: у себя «Избранное»/«Изменить», у других — «Сообщение»/
//      «Звук»/«Позвонить»/«Ещё» (высота 64, иконка + подпись);
//    • единый инфо-блок с иконками строк (канал-ссылка, био, день рождения,
//      @имя с копированием, «В Xipher с») + «Показать QR-код профиля»;
//    • «Знаки», «Подарки» (закреплённые + счётчик), «Общие медиа»;
//    • нижние действия (контакты / поделиться / блокировка) — как в вебе.
//  Правила приватности и состав действий решает СЕРВЕР (relation) — клиент
//  ничего не прячет сам.
// ─────────────────────────────────────────────────────────────────────────────
class ProfilePanel : public ModalOverlay {
    Q_OBJECT
public:
    ProfilePanel(ApiClient* api, QWidget* parent);

    // Известные имя/аватар из списка чатов: шапка рисуется мгновенно, сеть
    // дополняет остальное (knownPreview в view.js).
    void setKnownPreview(const QString& name, const QString& avatarUrl, bool online);
    void openFor(const QString& userId);
    // Полный ответ /api/profile/view (public — design-verify вызывает напрямую).
    void applyProfileView(qint64 reqId, const QJsonObject& data, bool ok, const QString& error);
    void applyMediaCount(const QString& chatId, int total);

signals:
    void messageRequested(const QString& userId);
    void callRequested(const QString& userId, const QString& name, const QString& avatarUrl);
    void savedMessagesRequested();
    void settingsRequested();
    void channelOpenRequested(const QString& channelId, const QString& name, const QString& link);
    void muteToggled(const QString& userId);
    void mediaRequested(const QString& userId, int total);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void renderSkeleton();
    void renderError(const QString& message);
    void renderProfile(const QJsonObject& data);
    QWidget* makeHead(const QJsonObject& p);
    QWidget* makeActions(const QJsonObject& p, const QJsonObject& rel);
    QPushButton* makeAction(Icons::Kind icon, const QString& label, bool enabled);
    QWidget* makeInfoRow(Icons::Kind icon, const QString& value, const QString& label,
                         bool copyable = false);
    QWidget* makeChannelRow(const QJsonObject& ch);
    QWidget* makeInfo(const QJsonObject& p);
    QWidget* makeMarks(const QJsonArray& marks);
    QWidget* makeGifts(const QJsonObject& gifts);
    QWidget* makeMediaEntry(const QJsonObject& p);
    QWidget* makeBottom(const QJsonObject& p, const QJsonObject& rel);
    void moreMenu(const QPoint& globalPos);
    void openQr();
    void openGiftDialog();
    void clearContent();
    void fitHeight();   // карточка растёт под контент (fit-content веба)
    void pushScreen(const QString& title, QWidget* screen);
    void popScreen();
    void afterAction();

    ApiClient* api_        = nullptr;
    QStackedLayout* stack_ = nullptr;
    QScrollArea*    scroll_ = nullptr;
    QVBoxLayout*    col_    = nullptr;     // колонка контента профиля
    QPushButton*    closeBtn_ = nullptr;   // крестик поверх баннера
    QWidget*        mediaRow_ = nullptr;   // строка «Общие медиа» (для счётчика)
    QString         userId_;
    QString         knownName_, knownAvatar_;
    bool            knownOnline_ = false;
    bool            hasPreview_  = false;
    QJsonObject     lastData_;             // для перечитывания после действия
    qint64          reqId_ = 0;            // id последнего отправленного запроса
    int             mediaTotal_ = -1;      // -1 — ещё не пришло
    bool            isSelf_ = false;
};
