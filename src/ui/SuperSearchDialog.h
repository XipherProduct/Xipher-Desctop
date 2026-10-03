#pragma once
#include <QWidget>
#include <QPointer>
#include <QList>
#include <QJsonArray>
#include <QSet>
#include <QDateTime>
#include <functional>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QJsonObject;
class QJsonArray;
class QAbstractButton;
class ApiClient;
class QTimer;
struct Chat;

// Клик-фильтр для строк результата (QWidget без сигнала clicked).
class SuperSearchClickFilter : public QObject {
    Q_OBJECT
public:
    explicit SuperSearchClickFilter(std::function<void()> cb, QObject* parent = nullptr)
        : QObject(parent), cb_(std::move(cb)) {}
    bool eventFilter(QObject* obj, QEvent* e) override;
private:
    std::function<void()> cb_;
};

// ─────────────────────────────────────────────────────────────────────────────
//  SuperSearchDialog — ЕДИНЫЙ поиск (слияние «обычного» и супер-поиска,
//  1:1 с supersearch.js веба):
//   • режимы «Обычный» / «Супер-поиск» (сегмент-переключатель);
//   • область «В этом чате» / «Во всех чатах» (во всех — параллельный
//     обход до 16 чатов, 4 воркера, результаты группируются по чатам);
//   • супер-режим парсит естественный язык: «найди фото…», «где обсуждали
//     цену?», чипы типов (фото/файлы/голосовые/ссылки/локации).
//  Клик по результату открывает нужный чат и подсвечивает сообщение.
// ─────────────────────────────────────────────────────────────────────────────
class SuperSearchDialog : public QWidget {
    Q_OBJECT
public:
    SuperSearchDialog(ApiClient* api, QWidget* parent);
    // chatId пустой → глобальный режим «Во всех чатах».
    void openFor(const QString& chatId, const QString& context);
    // Список чатов для области «Во всех чатах» (ChatPage отдаёт объединённый).
    void setChats(const QList<Chat>& chats) { chats_ = chats; }

    // DSL-фильтры Discord-стиля (SRC-01): from:@bob has:photo before:01.09
    // after:15.08 — распознаются в любом режиме, применяются к результатам
    // клиента; остальное уходит на сервер ключевыми словами.
    struct Dsl {
        QString fromUser;      // from:@bob / from:bob / from:<uuid>
        QString type;          // has:photo|file|link|voice|video|geo
        QDateTime before;      // до даты (включительно — строго раньше след. дня)
        QDateTime after;       // после даты
        QString keywords;      // текст без фильтров
        bool isEmpty() const {
            return fromUser.isEmpty() && type.isEmpty()
                   && !before.isValid() && !after.isValid();
        }
    };
    static Dsl parseDsl(const QString& raw);
    // Сообщение проходит клиентские фильтры (from/даты)? (для design-verify)
    static bool matchesDsl(const QJsonObject& msg, const Dsl& dsl);

signals:
    // Клик по результату → открыть чат (chatId) и прыгнуть к сообщению;
    // keywords — последний запрос (для подсветки в бабблах, SRC-04).
    void resultPicked(const QString& chatId, const QString& messageId,
                      const QString& keywords);
    // SRC-02: выбрана дата — открыть чат и догрузить до неё.
    void dateJumpRequested(const QString& chatId, const QString& isoDate);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onDebouncedSearch();
    void doSearch(const QString& keywords, const QString& type);
    void onResults(const QString& requestId, const QJsonArray& messages);

private:
    void buildUi();
    void setMode(bool superMode);
    void setScope(bool allChats);
    void clearResults();
    void addChatGroupHeader(const QString& title);
    void addResultRow(const QJsonObject& m);
    struct Parsed { QString type; QString keywords; };
    Parsed parseQuery(const QString& raw, bool super) const;

    ApiClient* api_;
    QWidget*   card_ = nullptr;
    QLineEdit* input_ = nullptr;
    QWidget*   resultsBox_ = nullptr;
    QAbstractButton* segNormal_ = nullptr;
    QAbstractButton* segSuper_ = nullptr;
    QAbstractButton* scopeChatBtn_ = nullptr;
    QAbstractButton* scopeAllBtn_ = nullptr;
    QList<QAbstractButton*> typeChips_;
    QTimer*    debounce_ = nullptr;

    QString chatId_, context_, chatName_;
    bool    superMode_ = true;
    bool    scopeAll_ = false;
    QSet<QString> activeReqIds_;    // живые запросы (scope=all шлёт пачку)
    QString pinnedType_;            // тип, выбранный чипом
    Dsl     dsl_;                   // активные DSL-фильтры (SRC-01)
    QLabel* filterBar_ = nullptr;   // строка активных фильтров под полем
    void renderFilterBar();         // чипы «from:@bob · has:photo · …»
    int     seq_ = 0;
    int     pending_ = 0;           // незавершённые запросы (scope=all)
    struct Acc { QString chatId, ctx, name; QJsonArray msgs; };
    QList<Acc> acc_;                // накопленные результаты по чатам
    QList<Chat> chats_;             // копия списка чатов (для имён/обхода)
    QString lastKeywords_, lastType_;
};
