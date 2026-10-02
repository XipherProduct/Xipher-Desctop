#pragma once
#include "ui/ModalOverlay.h"
#include "net/Models.h"

#include <QList>

class QLineEdit;
class QVBoxLayout;
class QFrame;

// ─────────────────────────────────────────────────────────────────────────────
//  QuickSwitcher — мгновенный переход между чатами по Ctrl+K (Discord-стиль,
//  DSC-01): нечёткий поиск по названию/username, ↑/↓ + Enter, Esc закрывает.
// Пустой запрос — весь список в порядке «свежести» (как пришёл с сервера).
// ─────────────────────────────────────────────────────────────────────────────
class QuickSwitcher : public ModalOverlay {
    Q_OBJECT
public:
    QuickSwitcher(const QList<Chat>& chats, QWidget* parent);
    // Список чатов мог обновиться, пока диалог был закрыт: подсунуть свежий
    // (пересобирает выдачу по текущему запросу).
    void setChats(const QList<Chat>& chats);
    // Перед showAnimated: очистить прошлый запрос и взять фокус в поле.
    void resetForOpen();

    // Тестовые швы (design-verify): текущая выдача и выделение.
    QStringList resultIds() const;
    QString selectedId() const;

signals:
    void picked(const Chat& chat);

protected:
    bool eventFilter(QObject* obj, QEvent* e) override;

private:
    void rebuild(const QString& filter);
    void moveSelection(int delta);
    void activateSelected();
    // Fuse-подобный скоринг: префикс названия > префикс слова > подпоследовательность.
    static int fuzzyScore(const Chat& c, const QString& query);

    QList<Chat>   chats_;
    QList<Chat>   results_;         // текущая выдача (в порядке скоринга)
    int           sel_ = 0;         // выделенная строка
    QLineEdit*    input_   = nullptr;
    QVBoxLayout*  listBox_ = nullptr;
    QList<QFrame*> rows_;           // виджеты строк выдачи
};
