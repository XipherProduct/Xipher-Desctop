#pragma once
#include "ui/ModalOverlay.h"
#include "ui/RichDoc.h"

#include <QList>

class QPlainTextEdit;
class QVBoxLayout;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
//  RichEditorDialog — составление рич-сообщения (RTE-02): блоки с тулбаром
//  (заголовки/цитата/список/код/разделитель/чекбокс), inline B/I/U/S —
//  markdown-обёртки выделения, «/» в пустом блоке — меню вставки блока.
//  Отправка собирает RichDoc → JSON-сообщение (RTE-01 round-trip).
// ─────────────────────────────────────────────────────────────────────────────
class RichEditorDialog : public ModalOverlay {
    Q_OBJECT
public:
    RichEditorDialog(QWidget* parent);

    // Тестовые шовы (design-verify).
    int blockCount() const { return editors_.size(); }
    int debugSlashCount() const { return slashShown_; }   // «/»-срабатывания
    void insertBlockForTest(RichBlock::Type t);
    RichDoc collectDoc() const;

signals:
    void sendRequested(const QString& richMessage);

private:
    void rebuild();
    void insertBlock(RichBlock::Type t, int after = -1);
    void wrapSelection(const QString& markup);   // **bold** / *italic* / …
    void showSlashMenu(QPlainTextEdit* ed, int blockIdx);

    struct BlockUi {
        RichBlock::Type type = RichBlock::Type::Para;
        QPlainTextEdit* editor = nullptr;   // hr — nullptr
        QWidget* row = nullptr;
    };
    QList<BlockUi> editors_;
    QVBoxLayout* listLay_ = nullptr;
    int slashShown_ = 0;   // сколько раз открыто slash-меню
    QPushButton* lastFocusToolbar_ = nullptr;
};
