#pragma once
#include <QWidget>
#include <QPointer>
#include <functional>

class QVBoxLayout;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QJsonObject;
class QJsonArray;
class ApiClient;
class QTimer;

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
//  SuperSearchDialog — супер-поиск по текущему чату (Ctrl+Shift+F),
//  1:1 с supersearch.js веб-клиента: natural-language парсер
//  («найди фото…», «где обсуждали цену?»), чипы быстрых фильтров,
//  подсветка совпадений, пагинация «Загрузить ещё».
// ─────────────────────────────────────────────────────────────────────────────
class SuperSearchDialog : public QWidget {
    Q_OBJECT
public:
    SuperSearchDialog(ApiClient* api, QWidget* parent);
    // Открыть для чата: context — «dm» или «group».
    void openFor(const QString& chatId, const QString& context);

signals:
    // Клик по результату → прыжок к сообщению в чате.
    void resultPicked(const QString& messageId);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private slots:
    void onDebouncedSearch();
    void doSearch(const QString& keywords, const QString& type);
    void loadMore();
    void onResults(const QString& requestId, const QJsonArray& messages);

private:
    void buildUi();
    void setBusy(bool busy);
    void clearResults();
    // Парсер запроса — порт parseQuery() из supersearch.js.
    struct Parsed { QString type; QString keywords; };
    Parsed parseQuery(const QString& raw) const;

    ApiClient* api_;
    QWidget*   card_;
    QLineEdit* input_ = nullptr;
    QWidget*   resultsBox_ = nullptr;
    QPushButton* moreBtn_ = nullptr;
    QTimer*    debounce_ = nullptr;

    QString chatId_, context_;
    QString reqId_;
    int     seq_ = 0;
    int     offset_ = 0;
    QString lastKeywords_, lastType_;
    bool    searching_ = false;
};
