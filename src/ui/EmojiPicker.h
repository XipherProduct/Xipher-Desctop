#pragma once
#include <QFrame>
#include <QStringList>
#include <QJsonArray>

class QScrollArea;
class QWidget;
class QGridLayout;
class QLineEdit;
class QPushButton;
class QStackedLayout;
class ApiClient;

// ─────────────────────────────────────────────────────────────────────────────
//  EmojiPicker — панель у поля ввода, 1:1 с .tg-emoji-panel веба:
//  приклеена к композеру (над ним, прижата к правому краю), НЕ попап —
//  остаётся открытой, пока печатаешь; табы [Эмодзи | Подарки], поиск,
//  кнопка ⌫; лента категорий (недавние + 8), сетка 9 колонок.
//  Недавние хранятся локально (Prefs, как localStorage веба, до 32).
//  Таб «Подарки» — каталог /api/gifts/catalog: клик по карточке →
//  giftSendRequested(giftId, name), отправку решает ChatPage (только ЛС).
// ─────────────────────────────────────────────────────────────────────────────
class EmojiPicker : public QFrame {
    Q_OBJECT
public:
    explicit EmojiPicker(QWidget* parent = nullptr);

    // Каталог подарков подтягивается сам при первом открытии таба.
    void setGiftApi(ApiClient* api);
    // Контекст: можно ли дарить в текущем чате (ЛС, не «Избранное»).
    void setGiftsAvailable(bool available);

    void openAbove(QWidget* anchor);   // показать, прижав к правому краю anchor
    void toggleAbove(QWidget* anchor);

signals:
    void emojiPicked(const QString& emoji);
    void backspacePressed();           // кнопка ⌫ в панели (как в Telegram)
    void giftSendRequested(const QString& giftId, const QString& name);

private:
    void buildCategories();
    void showCategory(int index);
    void showSearchResults(const QString& query);
    void loadRecents();
    void addRecent(const QString& emoji);
    QWidget* makeCell(const QString& emoji, QWidget* parent);
    void clearGrid(QWidget* host);
    void buildGiftsGrid();
    void setTab(int tab);              // 0 — эмодзи, 1 — подарки

    ApiClient*      giftApi_ = nullptr;
    bool            giftsAvailable_ = true;
    bool            giftsLoaded_ = false;
    QJsonArray      giftsCatalog_;

    QStackedLayout* stack_ = nullptr;
    QScrollArea*    scroll_ = nullptr;
    QWidget*        grid_    = nullptr;
    QWidget*        giftsPage_ = nullptr;
    QScrollArea*    giftsScroll_ = nullptr;
    QWidget*        giftsGrid_ = nullptr;
    QLineEdit*      search_ = nullptr;
    QList<QStringList> categories_;   // 0 — недавние, 1..8 — разделы
    QList<QPushButton*> catButtons_;
    QPushButton*    tabEmoji_ = nullptr;
    QPushButton*    tabGifts_ = nullptr;
    QWidget*        catBar_ = nullptr;
    QStringList     recents_;
    int current_ = 0;
    int currentTab_ = 0;
};
