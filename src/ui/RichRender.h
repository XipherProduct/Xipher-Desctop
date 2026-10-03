#pragma once
#include "ui/RichDoc.h"
#include <QWidget>

// ─────────────────────────────────────────────────────────────────────────────
//  RichMessageWidget — рендер рич-сообщения одним QPainter (RTE-03):
//  заголовки/абзацы (QTextLayout wordwrap), цитаты (полоса accent), списки,
//  код-блоки (моно, фон), разделители, кликабельные чекбоксы.
//  Опорный прогон 20 блоков укладывается в кадр (≤16 мс, см. design-verify).
// ─────────────────────────────────────────────────────────────────────────────
class RichMessageWidget : public QWidget {
    Q_OBJECT
public:
    RichMessageWidget(const RichDoc& doc, QWidget* parent = nullptr);

    int heightForWidth(int w) const override;
    QSize sizeHint() const override { return {420, heightForWidth(420)}; }

    int blockCount() const { return doc_.blocks.size(); }
    qint64 lastPaintMs() const { return lastPaintMs_; }   // метрика ≤16 мс

signals:
    void checkboxToggled(int blockIndex, bool checked);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    struct Item {
        int block = -1;
        QRect rect;          // зона текста/элемента
        QString text;
        int fontSize = 15;
        bool bold = false;
        bool mono = false;
        bool quote = false;  // левая полоса + курсивный оттенок
        bool divider = false;
        bool checkbox = false;
        bool checked = false;
        bool bullet = false;
    };
    const QList<Item>& layoutItems(int width) const;   // кэш по ширине
    int contentWidth() const;

    RichDoc doc_;
    mutable int cachedWidth_ = -1;
    mutable QList<Item> items_;
    qint64 lastPaintMs_ = 0;
    int hoverCheckbox_ = -1;
};
