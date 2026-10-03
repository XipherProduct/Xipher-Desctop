#include "ui/RichRender.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QTextLayout>
#include <QElapsedTimer>

namespace {
constexpr int kPad = 4;        // вертикальный зазор между блоками
constexpr int kCodePad = 8;
}

RichMessageWidget::RichMessageWidget(const RichDoc& doc, QWidget* parent)
    : QWidget(parent), doc_(doc) {
    setMouseTracking(true);
    // Высота тянется под контент: layout пересчитывается на resize.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

int RichMessageWidget::contentWidth() const {
    return qMax(120, width() - 8);
}

const QList<RichMessageWidget::Item>& RichMessageWidget::layoutItems(int width) const {
    if (cachedWidth_ == width && !items_.isEmpty()) return items_;
    items_.clear();
    cachedWidth_ = width;
    int y = 0;
    for (int bi = 0; bi < doc_.blocks.size(); ++bi) {
        const RichBlock& b = doc_.blocks[bi];
        switch (b.type) {
            case RichBlock::Type::H1:
            case RichBlock::Type::H2:
            case RichBlock::Type::H3: {
                Item it;
                it.block = bi;
                const int h = b.type == RichBlock::Type::H1 ? 22
                            : b.type == RichBlock::Type::H2 ? 19 : 17;
                it.fontSize = h;
                it.bold = true;
                QFont f = font();
                f.setPointSizeF(font().pointSizeF() * h / 15.0);
                const int lines = 1;   // заголовки короткие — одна строка с переносом по словам
                QFontMetrics fm(f);
                const QRect br = fm.boundingRect(QRect(0, 0, width, 1000),
                                                 Qt::TextWordWrap, b.text);
                it.rect = QRect(0, y, width, br.height());
                it.text = b.text;
                y += br.height() + kPad + 4;
                items_.append(it);
                break;
            }
            case RichBlock::Type::Para: {
                Item it;
                it.block = bi;
                QFontMetrics fm(font());
                const QRect br = fm.boundingRect(QRect(0, 0, width, 100000),
                                                 Qt::TextWordWrap, b.text);
                it.rect = QRect(0, y, width, br.height());
                it.text = b.text;
                y += br.height() + kPad;
                items_.append(it);
                break;
            }
            case RichBlock::Type::Quote: {
                Item it;
                it.block = bi;
                it.quote = true;
                QFontMetrics fm(font());
                const QRect br = fm.boundingRect(QRect(0, 0, width - 14, 100000),
                                                 Qt::TextWordWrap, b.text);
                it.rect = QRect(12, y, width - 12, br.height() + 6);
                it.text = b.text;
                y += br.height() + 6 + kPad;
                items_.append(it);
                break;
            }
            case RichBlock::Type::List: {
                QFontMetrics fm(font());
                for (int li = 0; li < b.items.size(); ++li) {
                    Item it;
                    it.block = bi;
                    it.bullet = true;
                    const QRect br = fm.boundingRect(QRect(0, 0, width - 20, 100000),
                                                     Qt::TextWordWrap, b.items[li]);
                    it.rect = QRect(18, y, width - 18, br.height());
                    it.text = b.items[li];
                    y += br.height() + 2;
                    items_.append(it);
                }
                y += kPad;
                break;
            }
            case RichBlock::Type::Code: {
                Item it;
                it.block = bi;
                it.mono = true;
                QFont f = font();
                f.setFamily(QStringLiteral("JetBrains Mono"));
                if (f.family() != QStringLiteral("JetBrains Mono"))
                    f.setFamily(QStringLiteral("monospace"));
                f.setPointSizeF(font().pointSizeF() * 0.92);
                QFontMetrics fm(f);
                const int lines = qMax(1, int(b.text.count(QLatin1Char('\n'))) + 1);
                const int h = lines * fm.lineSpacing() + kCodePad * 2;
                it.rect = QRect(0, y, width, h);
                it.text = b.text;
                y += h + kPad;
                items_.append(it);
                break;
            }
            case RichBlock::Type::Divider: {
                Item it;
                it.block = bi;
                it.divider = true;
                it.rect = QRect(0, y + 8, width, 1);
                y += 17;
                items_.append(it);
                break;
            }
            case RichBlock::Type::Checkbox: {
                Item it;
                it.block = bi;
                it.checkbox = true;
                it.checked = b.checked;
                QFontMetrics fm(font());
                const QRect br = fm.boundingRect(QRect(0, 0, width - 30, 100000),
                                                 Qt::TextWordWrap, b.text);
                const int h = qMax(22, br.height());
                it.rect = QRect(22, y, width - 22, h);
                it.text = b.text;
                y += h + 3;
                items_.append(it);
                break;
            }
        }
    }
    return items_;
}

int RichMessageWidget::heightForWidth(int w) const {
    const auto& items = layoutItems(w);
    int bottom = 0;
    for (const Item& it : items) bottom = qMax(bottom, it.rect.bottom());
    return bottom + 6;
}

void RichMessageWidget::paintEvent(QPaintEvent*) {
    QElapsedTimer tm;
    tm.start();
    QPainter p(this);
    const int w = contentWidth() + 8;
    const auto& items = layoutItems(w);

    for (const Item& it : items) {
        if (it.divider) {
            p.setPen(QColor(255, 255, 255, 30));
            p.drawLine(it.rect.left(), it.rect.y(), it.rect.right(), it.rect.y());
            continue;
        }
        if (it.mono) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x0B, 0x0A, 0x0E));
            p.drawRoundedRect(it.rect.adjusted(0, 0, 0, 0), 8, 8);
            p.setBrush(QColor(0x22, 0x1F, 0x2C));
            p.setPen(QColor(0x22, 0x1F, 0x2C));
            p.drawRoundedRect(it.rect.adjusted(0, 0, 0, 0), 8, 8);
            QFont f = font();
            f.setFamily(QStringLiteral("JetBrains Mono"));
            if (f.family() != QStringLiteral("JetBrains Mono"))
                f.setFamily(QStringLiteral("monospace"));
            f.setPointSizeF(font().pointSizeF() * 0.92);
            p.setFont(f);
            p.setPen(QColor(0xD0, 0xBF, 0xF1));
            p.drawText(it.rect.adjusted(kCodePad, kCodePad, -kCodePad, -kCodePad),
                       Qt::AlignTop | Qt::TextWordWrap, it.text);
            continue;
        }
        if (it.quote) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x8B, 0x5C, 0xF6));
            p.drawRoundedRect(QRect(0, it.rect.y(), 3, it.rect.height()), 2, 2);
        }
        QFont f = font();
        if (it.bold) f.setBold(true);
        if (it.quote) f.setItalic(true);
        if (it.fontSize != 15) f.setPointSizeF(font().pointSizeF() * it.fontSize / 15.0);
        p.setFont(f);
        p.setPen(QColor(0xF3, 0xF1, 0xF8));

        if (it.checkbox) {
            const QRect box(0, it.rect.y() + qMax(0, (it.rect.height() - 18) / 2), 18, 18);
            p.setBrush(it.checked ? QColor(0x8B, 0x5C, 0xF6) : QColor(0x1A, 0x18, 0x22));
            p.setPen(hoverCheckbox_ == it.block ? QColor(0x8B, 0x5C, 0xF6)
                                                : QColor(255, 255, 255, 60));
            p.drawRoundedRect(box, 5, 5);
            if (it.checked) {
                p.setPen(QPen(QColor(255, 255, 255), 2));
                p.drawLine(box.left() + 4, box.center().y(),
                           box.center().x() - 1, box.bottom() - 4);
                p.drawLine(box.center().x() - 1, box.bottom() - 4,
                           box.right() - 3, box.top() + 4);
            }
            p.setPen(it.checked ? QColor(0x72, 0x6C, 0x82) : QColor(0xF3, 0xF1, 0xF8));
            p.drawText(it.rect, Qt::AlignTop | Qt::TextWordWrap, it.text);
            continue;
        }
        if (it.bullet) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x8B, 0x5C, 0xF6));
            p.drawEllipse(QRect(4, it.rect.y() + 6, 6, 6));
            p.setPen(QColor(0xF3, 0xF1, 0xF8));
        }
        p.drawText(it.rect, Qt::AlignTop | Qt::TextWordWrap, it.text);
    }
    lastPaintMs_ = tm.nsecsElapsed() / 1000000;
}

void RichMessageWidget::mousePressEvent(QMouseEvent* e) {
    const int w = contentWidth() + 8;
    const auto& items = layoutItems(w);
    for (const Item& it : items) {
        // Чекбокс: клик по всей строке переключает (как в TG).
        if (it.checkbox && e->pos().y() >= it.rect.y()
            && e->pos().y() < it.rect.bottom() && e->pos().x() < it.rect.right()) {
            RichBlock& b = doc_.blocks[it.block];
            b.checked = !b.checked;
            emit checkboxToggled(it.block, b.checked);
            update();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void RichMessageWidget::mouseMoveEvent(QMouseEvent* e) {
    const int w = contentWidth() + 8;
    const auto& items = layoutItems(w);
    int hover = -1;
    for (const Item& it : items)
        if (it.checkbox && e->pos().y() >= it.rect.y()
            && e->pos().y() < it.rect.bottom() && e->pos().x() < it.rect.right())
            hover = it.block;
    if (hover != hoverCheckbox_) {
        hoverCheckbox_ = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}
