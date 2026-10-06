#include "ui/ImageEditor.h"

#include <QComboBox>
#include <QLabel>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QMouseEvent>
#include <QPainter>
#include <QLineEdit>
#include <QEvent>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
const char* kToolNames[7] = {"🖌 Кисть", "🖍 Маркер", "⌫ Ластик",
                             "→ Стрелка", "▭ Прямоуг.", "◯ Эллипс", "T Текст"};
const QColor kPalette[] = {
    QColor(0xF2, 0x3F, 0x71), QColor(0xF5, 0x9E, 0x0B), QColor(0x22, 0xC5, 0x5E),
    QColor(0x3B, 0x82, 0xF6), QColor(0x8B, 0x5C, 0xF6), QColor(0xF3, 0xF1, 0xF8),
    QColor(0x0B, 0x0A, 0x0E),
};
} // namespace

ImageEditorDialog::ImageEditorDialog(const QByteArray& bytes, QWidget* parent)
    : QDialog(parent) {
    src_ = QImage::fromData(bytes);
    if (src_.isNull()) src_ = QImage(800, 600, QImage::Format_ARGB32);
    // Холст вмещаем в окно: скейлим исходник (правки в координатах превью).
    const QSize maxSz(880, 560);
    if (src_.size().width() > maxSz.width() || src_.size().height() > maxSz.height())
        src_ = src_.scaled(maxSz, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    setWindowTitle(QStringLiteral("Редактор фото"));
    setModal(true);
    setStyleSheet(QStringLiteral(
        "QDialog{background:#131218;} QLabel{color:#ACA6BD;font-size:12px;}"
        "QToolButton{background:#1A1822;border:1px solid rgba(255,255,255,10%);"
        "border-radius:9px;color:#ACA6BD;font-size:12px;padding:5px 9px;}"
        "QToolButton:hover{background:#221F2C;color:#F3F1F8;}"
        "QToolButton:checked{background:rgba(139,92,246,30%);color:#F3F1F8;"
        "border-color:rgba(139,92,246,60%);}"
        "QComboBox,QLineEdit{background:#1A1822;border:1px solid rgba(255,255,255,10%);"
        "border-radius:9px;color:#F3F1F8;font-size:12px;padding:5px 8px;min-width:60px;}"
        "QPushButton{border-radius:9px;font-size:13px;padding:7px 14px;}"
        "#okBtn{background:#8B5CF6;color:#fff;font-weight:600;}"
        "#okBtn:hover{background:#9B72F8;}"
        "#undoBtn,#cancelBtn{background:transparent;border:1px solid rgba(255,255,255,14%);color:#ACA6BD;}"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    // Тулбар: инструменты, толщина, палитра, undo, Ok/Отмена.
    auto* tb = new QHBoxLayout();
    tb->setSpacing(5);
    for (int i = 0; i < 7; ++i) {
        auto* b = new QToolButton(this);
        b->setText(QLatin1String(kToolNames[i]));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        if (i == 0) b->setChecked(true);
        const int t = i;
        connect(b, &QToolButton::clicked, this, [this, t, i]() {
            setTool(t);
            for (int k = 0; k < 7; ++k) toolBtns_[k]->setChecked(k == i);
        });
        toolBtns_[i] = b;
        tb->addWidget(b);
    }
    auto* widthBox = new QComboBox(this);
    widthBox->addItems({QStringLiteral("2"), QStringLiteral("4"),
                        QStringLiteral("8"), QStringLiteral("14")});
    widthBox->setCurrentIndex(1);
    connect(widthBox, &QComboBox::currentIndexChanged, this, [this](int i) {
        width_ = (i == 0 ? 2 : i == 1 ? 4 : i == 2 ? 8 : 14);
    });
    widthBox_ = widthBox;
    tb->addWidget(widthBox);
    for (const QColor& c : kPalette) {
        auto* b = new QToolButton(this);
        b->setFixedSize(22, 22);
        b->setStyleSheet(QStringLiteral(
            "QToolButton{background:%1;border:1px solid rgba(255,255,255,25%);"
            "border-radius:11px;} QToolButton:hover{border:2px solid #F3F1F8;}")
            .arg(c.name()));
        connect(b, &QToolButton::clicked, this, [this, c]() { color_ = c; });
        tb->addWidget(b);
    }
    tb->addStretch();
    auto* undoBtn = new QPushButton(QStringLiteral("↩ Вернуть"), this);
    undoBtn->setObjectName(QStringLiteral("undoBtn"));
    connect(undoBtn, &QPushButton::clicked, this, [this]() { pushUndo(); });
    tb->addWidget(undoBtn);
    root->addLayout(tb);

    // Подсказка ввода текста (IMG-02): что печатается при инструменте «T».
    auto* textRow = new QHBoxLayout();
    auto* cap = new QLabel(QStringLiteral("Текст на картинке:"), this);
    textInput_ = new QLineEdit(this);
    textInput_->setPlaceholderText(QStringLiteral("печатайте и кликните место"));
    textRow->addWidget(cap);
    textRow->addWidget(textInput_, 1);
    root->addLayout(textRow);

    canvas_ = new QWidget(this);
    canvas_->setMinimumSize(src_.size());
    canvas_->setStyleSheet(QStringLiteral("background:#0B0A0E;border-radius:12px;"));
    canvas_->setMouseTracking(true);
    canvas_->installEventFilter(this);
    root->addWidget(canvas_, 1, Qt::AlignHCenter);

    auto* actions = new QHBoxLayout();
    actions->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Отмена"), this);
    cancel->setObjectName(QStringLiteral("cancelBtn"));
    connect(cancel, &QPushButton::clicked, this, [this]() { reject(); });
    auto* ok = new QPushButton(QStringLiteral("Отправить"), this);
    ok->setObjectName(QStringLiteral("okBtn"));
    connect(ok, &QPushButton::clicked, this, [this]() {
        QImage out(src_.size(), QImage::Format_ARGB32);
        QPainter p(&out);
        p.drawImage(0, 0, src_);
        renderStrokes(&p);
        p.end();
        QBuffer buf(&result_);
        out.save(&buf, "PNG");
        emit doneEditing();
        accept();
    });
    actions->addWidget(cancel);
    actions->addWidget(ok);
    root->addLayout(actions);

    rebuildCache();
}

void ImageEditorDialog::setTool(int t) {
    tool_ = t;
    if (t == 2) color_ = QColor(0x0B, 0x0A, 0x0E);          // ластик — цвет фона
    else if (color_ == QColor(0x0B, 0x0A, 0x0E)) color_ = QColor(0xF2, 0x3F, 0x71);
    if (canvas_) canvas_->setCursor(t == 6 ? Qt::CrossCursor : Qt::CrossCursor);
}

void ImageEditorDialog::pushUndo() {
    if (strokes_.isEmpty()) return;
    undo_.append(strokes_.takeLast());
    rebuildCache();
}

void ImageEditorDialog::rebuildCache() {
    cache_ = QPixmap::fromImage(src_).copy();
    QPainter p(&cache_);
    renderStrokes(&p);
    p.end();
    if (canvas_) canvas_->update();
}

void ImageEditorDialog::renderStrokes(QPainter* p) const {
    p->setRenderHint(QPainter::Antialiasing);
    for (const Stroke& s : strokes_) {
        QPen pen(s.color, s.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        if (s.tool == 1) {   // маркер — полупрозрачный и толще
            QColor c = s.color; c.setAlpha(110);
            pen = QPen(c, s.width * 2 + 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        }
        p->setPen(pen);
        p->setBrush(Qt::NoBrush);
        switch (s.tool) {
            case 0: case 1: case 2:
                p->drawLine(s.a, s.b);
                break;
            case 3: {   // стрелка: линия + наконечник
                p->drawLine(s.a, s.b);
                const qreal ang = std::atan2(s.b.y() - s.a.y(), s.b.x() - s.a.x());
                const int len = qMax(12, s.width * 4);
                p->drawLine(s.b, s.b - QPoint(int(len * cos(ang - 0.5)), int(len * sin(ang - 0.5))));
                p->drawLine(s.b, s.b - QPoint(int(len * cos(ang + 0.5)), int(len * sin(ang + 0.5))));
                break;
            }
            case 4:
                p->drawRect(QRect(s.a, s.b).normalized());
                break;
            case 5:
                p->drawEllipse(QRect(s.a, s.b).normalized());
                break;
            case 6: {   // текст: выравнивание по точке, крупно
                QFont f = p->font();
                f.setPixelSize(qMax(16, s.width * 6));
                f.setBold(true);
                p->setFont(f);
                p->drawText(s.a + QPoint(4, 4), s.text);
                break;
            }
        }
    }
}

bool ImageEditorDialog::eventFilter(QObject* obj, QEvent* e) {
    if (obj != canvas_) return QDialog::eventFilter(obj, e);
    // На этапе сборки layout-а летают не-мышьи события: позиция только у мыши.
    const auto* me = dynamic_cast<const QMouseEvent*>(e);
    if (!me) return QDialog::eventFilter(obj, e);
    const QPoint pos = me->position().toPoint();
    if (e->type() == QEvent::MouseButtonPress) {
        drawing_ = true;
        cur_ = Stroke{tool_, pos, pos, color_, width_,
                      tool_ == 6 ? textInput_->text() : QString()};
        return true;
    }
    if (e->type() == QEvent::MouseMove && drawing_) {
        if (tool_ == 0 || tool_ == 1 || tool_ == 2) {
            // Свободная линия = цепочка коротких штрихов.
            strokes_.append(Stroke{cur_.tool, cur_.b, pos, cur_.color, cur_.width});
            cur_.b = pos;
        } else {
            cur_.b = pos;
        }
        // Живой предпросмотр: кэш + текущий штрих поверх.
        rebuildCache();
        if (tool_ >= 3 && tool_ <= 5) {
            QPainter p(&cache_);
            strokes_.append(cur_);
            renderStrokes(&p);
            strokes_.removeLast();
        }
        return true;
    }
    if (e->type() == QEvent::MouseButtonRelease && drawing_) {
        drawing_ = false;
        if (tool_ >= 3 && tool_ <= 6) strokes_.append(cur_);
        undo_.clear();
        rebuildCache();
        return true;
    }
    return QDialog::eventFilter(obj, e);
}

void ImageEditorDialog::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (!cache_.isNull() && canvas_)
        p.drawPixmap(canvas_->x(), canvas_->y(), cache_);
}

// Тестовые швы (design-verify): программный штрих + превью.
void ImageEditorDialog::addStrokeForTest(int tool, const QPoint& a, const QPoint& b,
                                         const QString& text) {
    Stroke s{tool, a, b, color_, width_, text};
    strokes_.append(s);
    rebuildCache();
}

QImage ImageEditorDialog::renderPreview() {
    QImage out(src_.size(), QImage::Format_ARGB32);
    QPainter p(&out);
    p.drawImage(0, 0, src_);
    renderStrokes(&p);
    p.end();
    return out;
}
