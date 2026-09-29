#pragma once
#include <QFrame>
#include <QStringList>

class QScrollArea;
class QWidget;
class QGridLayout;
class QLineEdit;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
//  EmojiPicker — панель эмодзи 1:1 с логикой Telegram / tg-emoji-panel веба:
//  строка поиска, лента категорий (недавние + 8 разделов), большая сетка.
//  Недавние хранятся локально (Prefs, как localStorage веба, до 32).
//  Открывается всплывающим окном (Qt::Popup), клик вне — закрывает.
// ─────────────────────────────────────────────────────────────────────────────
class EmojiPicker : public QFrame {
    Q_OBJECT
public:
    explicit EmojiPicker(QWidget* parent = nullptr);

signals:
    void emojiPicked(const QString& emoji);
    void backspacePressed();   // кнопка ⌫ в панели (как в Telegram)

private:
    void buildCategories();
    void showCategory(int index);
    void showSearchResults(const QString& query);
    void loadRecents();
    void addRecent(const QString& emoji);
    QWidget* makeCell(const QString& emoji, QWidget* parent);

    QScrollArea* scroll_ = nullptr;
    QWidget*     grid_    = nullptr;
    QLineEdit*   search_  = nullptr;
    QList<QStringList> categories_;   // 0 — недавние, 1..8 — разделы
    QList<QPushButton*> catButtons_;
    QStringList  recents_;
    int current_ = 0;
};
