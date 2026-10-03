#include "ui/RichEditor.h"
#include <QtGlobal>

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

QString blockTitle(RichBlock::Type t) {
    switch (t) {
        case RichBlock::Type::H1: return QStringLiteral("Заголовок 1");
        case RichBlock::Type::H2: return QStringLiteral("Заголовок 2");
        case RichBlock::Type::H3: return QStringLiteral("Заголовок 3");
        case RichBlock::Type::Para: return QStringLiteral("Абзац");
        case RichBlock::Type::Quote: return QStringLiteral("Цитата");
        case RichBlock::Type::List: return QStringLiteral("Список");
        case RichBlock::Type::Code: return QStringLiteral("Код");
        case RichBlock::Type::Divider: return QStringLiteral("Разделитель");
        case RichBlock::Type::Checkbox: return QStringLiteral("Чекбокс");
    }
    return QStringLiteral("Абзац");
}

} // namespace

RichEditorDialog::RichEditorDialog(QWidget* parent)
    : ModalOverlay(parent, 560) {
    card()->setStyleSheet(QStringLiteral(R"QSS(
#modalCard{background:#17151E;border:1px solid rgba(255,255,255,0.08);border-radius:18px;}
QLabel{color:#F3F1F8;}
#richTitle{font-size:17px;font-weight:800;}
#richTb{background:transparent;border:none;border-radius:9px;color:#ACA6BD;
  font-size:13px;font-weight:700;min-height:30px;padding:0 10px;}
#richTb:hover{background:#221F2C;color:#F3F1F8;}
#richSend{background:#8B5CF6;border:none;border-radius:10px;color:#fff;
  font-size:14px;font-weight:600;min-height:36px;padding:0 18px;}
#richSend:hover{background:#9B72F8;}
QPlainTextEdit{background:#131218;border:1px solid rgba(255,255,255,0.10);
  border-radius:10px;color:#F3F1F8;font-size:14px;padding:8px 10px;}
QPlainTextEdit:focus{border:1px solid #8B5CF6;}
#richBlockHead{color:#726C82;font-size:11px;font-weight:700;text-transform:uppercase;}
#richDel{background:transparent;border:none;color:#726C82;font-size:13px;}
#richDel:hover{color:#F87171;}
QScrollArea{background:transparent;border:none;}
)QSS"));

    auto* lay = cardLayout();
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(8);

    auto* head = new QHBoxLayout();
    auto* title = new QLabel(QStringLiteral("✨ Форматированное сообщение"), card());
    title->setObjectName(QStringLiteral("richTitle"));
    head->addWidget(title);
    head->addStretch();
    auto* send = new QPushButton(QStringLiteral("Отправить"), card());
    send->setObjectName(QStringLiteral("richSend"));
    connect(send, &QPushButton::clicked, this, [this]() {
        const QString msg = collectDoc().toMessage();
        if (!msg.isEmpty()) {
            emit sendRequested(msg);
            closeAnimated();
        }
    });
    head->addWidget(send);
    lay->addLayout(head);

    // Тулбар (RTE-02): типы блоков + inline B/I/U/S (markdown-обёртки).
    auto* tb = new QHBoxLayout();
    tb->setSpacing(2);
    auto mk = [this, tb](const QString& label, auto fn) {
        auto* b = new QPushButton(label, card());
        b->setObjectName(QStringLiteral("richTb"));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, fn);
        tb->addWidget(b);
        return b;
    };
    mk(QStringLiteral("H1"), [this]() { insertBlock(RichBlock::Type::H1); });
    mk(QStringLiteral("H2"), [this]() { insertBlock(RichBlock::Type::H2); });
    mk(QStringLiteral("H3"), [this]() { insertBlock(RichBlock::Type::H3); });
    mk(QStringLiteral("❝ Цитата"), [this]() { insertBlock(RichBlock::Type::Quote); });
    mk(QStringLiteral("≡ Список"), [this]() { insertBlock(RichBlock::Type::List); });
    mk(QStringLiteral("</> Код"), [this]() { insertBlock(RichBlock::Type::Code); });
    mk(QStringLiteral("— Разделитель"), [this]() { insertBlock(RichBlock::Type::Divider); });
    mk(QStringLiteral("☑ Чекбокс"), [this]() { insertBlock(RichBlock::Type::Checkbox); });
    tb->addStretch();
    mk(QStringLiteral("B"), [this]() { wrapSelection(QStringLiteral("**")); });
    mk(QStringLiteral("I"), [this]() { wrapSelection(QStringLiteral("*")); });
    mk(QStringLiteral("U"), [this]() { wrapSelection(QStringLiteral("__")); });
    mk(QStringLiteral("S"), [this]() { wrapSelection(QStringLiteral("~~")); });
    lay->addLayout(tb);

    auto* hint = new QLabel(QStringLiteral(
        "«/» в пустом блоке — меню вставки. B/I/U/S оборачивают выделение."), card());
    hint->setStyleSheet(QStringLiteral("color:#726C82;font-size:12px;"));
    lay->addWidget(hint);

    auto* sa = new QScrollArea(card());
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sa->setFixedHeight(360);
    auto* listW = new QWidget();
    listLay_ = new QVBoxLayout(listW);
    listLay_->setContentsMargins(0, 0, 4, 0);
    listLay_->setSpacing(8);
    listLay_->addStretch();
    sa->setWidget(listW);
    lay->addWidget(sa, 1);

    insertBlock(RichBlock::Type::Para);   // старт: один абзац
}

void RichEditorDialog::insertBlock(RichBlock::Type t, int after) {
    BlockUi bui;
    bui.type = t;

    auto* row = new QWidget();
    auto* rl = new QHBoxLayout(row);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(6);
    auto* kind = new QLabel(QStringLiteral("%1").arg(blockTitle(t)), row);
    kind->setObjectName(QStringLiteral("richBlockHead"));
    rl->addWidget(kind);
    rl->addStretch();

    if (t != RichBlock::Type::Divider) {
        auto* ed = new QPlainTextEdit();
        ed->setTabChangesFocus(true);
        if (t == RichBlock::Type::H1 || t == RichBlock::Type::H2 || t == RichBlock::Type::H3) {
            QFont f = ed->font();
            f.setBold(true);
            f.setPointSizeF(f.pointSizeF() * (t == RichBlock::Type::H1 ? 1.3
                                      : t == RichBlock::Type::H2 ? 1.15 : 1.05));
            ed->setFont(f);
        }
        if (t == RichBlock::Type::Code) {
            QFont f = ed->font();
            f.setFamily(QStringLiteral("JetBrains Mono"));
            ed->setFont(f);
            ed->setPlaceholderText(QStringLiteral("код"));
        }
        ed->setFixedHeight(t == RichBlock::Type::Code ? 84 : 40);
        ed->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ed->installEventFilter(this);
        // Slash-меню (RTE-02): «/» в пустом поле.
        connect(ed, &QPlainTextEdit::textChanged, this, [this, ed]() {
            const QString txt = ed->toPlainText();
            if (txt == QLatin1String("/")) {
                ed->blockSignals(true);
                ed->setPlainText(QString());
                ed->blockSignals(false);
                int idx = -1;
                for (int i = 0; i < editors_.size(); ++i)
                    if (editors_[i].editor == ed) { idx = i; break; }
                showSlashMenu(ed, idx);
            }
        });
        bui.editor = ed;
        rl->addWidget(ed, 1);
    } else {
        auto* hr = new QLabel(QStringLiteral("──────────────"), row);
        hr->setStyleSheet(QStringLiteral("color:#726C82;font-size:13px;"));
        rl->addWidget(hr, 0, Qt::AlignVCenter);
        rl->addStretch();
    }
    auto* del = new QPushButton(QStringLiteral("✕"), row);
    del->setObjectName(QStringLiteral("richDel"));
    del->setCursor(Qt::PointingHandCursor);
    del->setFixedSize(26, 26);
    row->setStyleSheet(QStringLiteral("background:transparent;"));
    const int pos = after < 0 ? editors_.size() : after + 1;
    connect(del, &QPushButton::clicked, this, [this, row]() {
        for (int i = 0; i < editors_.size(); ++i)
            if (editors_[i].row == row) { editors_.removeAt(i); break; }
        row->deleteLater();
    });
    bui.row = row;
    editors_.insert(pos, bui);
    listLay_->insertWidget(pos, row);
    if (bui.editor) bui.editor->setFocus();
}

void RichEditorDialog::insertBlockForTest(RichBlock::Type t) {
    insertBlock(t);
}

void RichEditorDialog::showSlashMenu(QPlainTextEdit* ed, int blockIdx) {
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu{background:#1A1822;border:1px solid rgba(255,255,255,0.12);"
        "border-radius:10px;color:#F3F1F8;} QMenu::item{padding:7px 16px;}"));
    const QList<QPair<QString, RichBlock::Type>> defs = {
        {QStringLiteral("Заголовок 1"), RichBlock::Type::H1},
        {QStringLiteral("Заголовок 2"), RichBlock::Type::H2},
        {QStringLiteral("Цитата"), RichBlock::Type::Quote},
        {QStringLiteral("Список"), RichBlock::Type::List},
        {QStringLiteral("Код"), RichBlock::Type::Code},
        {QStringLiteral("Разделитель"), RichBlock::Type::Divider},
        {QStringLiteral("Чекбокс"), RichBlock::Type::Checkbox},
    };
    for (const auto& d : defs)
        connect(menu.addAction(d.first), &QAction::triggered, this, [this, d, ed, blockIdx]() {
            // Пустой абзац превращается в выбранный тип, новый — не плодим.
            if (blockIdx >= 0 && blockIdx < editors_.size()
                && editors_[blockIdx].type == RichBlock::Type::Para
                && editors_[blockIdx].editor == ed
                && ed->toPlainText().isEmpty()) {
                // пересобрать этот блок: удалить и вставить на место
                editors_.removeAt(blockIdx);
                QWidget* row = ed->parentWidget();
                insertBlock(d.second, blockIdx - 1);
                row->deleteLater();
            } else {
                insertBlock(d.second, blockIdx);
            }
        });
    ++slashShown_;
    if (qEnvironmentVariableIsSet("DV_TEST")) return;   // тесты: без блокирующего exec
    QPoint pos = ed->mapToGlobal(QPoint(0, ed->height() + 4));
    menu.exec(pos);
}

void RichEditorDialog::wrapSelection(const QString& markup) {
    for (const BlockUi& b : editors_) {
        if (!b.editor || !b.editor->hasFocus()) continue;
        QTextCursor c = b.editor->textCursor();
        if (c.hasSelection()) {
            const QString sel = c.selectedText();
            c.insertText(markup + sel + markup);
        } else {
            b.editor->insertPlainText(markup + markup);
            QTextCursor back = b.editor->textCursor();
            back.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, markup.size());
            b.editor->setTextCursor(back);
        }
        return;
    }
}

RichDoc RichEditorDialog::collectDoc() const {
    RichDoc d;
    for (const BlockUi& b : editors_) {
        RichBlock rb;
        rb.type = b.type;
        if (b.type == RichBlock::Type::List) {
            const QStringList lines = b.editor->toPlainText()
                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            rb.items = lines;
            if (lines.isEmpty()) continue;
        } else if (b.type == RichBlock::Type::Divider) {
            // без текста
        } else {
            rb.text = b.editor->toPlainText();
            if (rb.text.isEmpty()) continue;
        }
        d.blocks.append(rb);
    }
    return d;
}
