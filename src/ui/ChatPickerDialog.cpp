#include "ui/Theme.h"
#include "ui/ChatPickerDialog.h"
#include "ui/AvatarUtil.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QFrame>
#include <QEvent>

ChatPickerDialog::ChatPickerDialog(const QList<Chat>& chats, const QString& title, QWidget* parent)
    : ModalOverlay(parent, 440), chats_(chats) {
    card()->setFixedHeight(560);
    card()->setStyleSheet(ThemePreset::applyTokens(QStringLiteral(R"QSS(
#modalCard{background:@{s1};border:1px solid @{bDef};border-radius:18px;}
QLabel{color:@{tp};}
#dlgHeader{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 @{acD},stop:0.55 @{ac},stop:1 #B06CF0);
  border-top-left-radius:18px;border-top-right-radius:18px;}
#dlgTitle{font-size:17px;font-weight:800;color:#fff;}
#closeBtn{background:@{bStr};border:none;border-radius:16px;color:#fff;font-size:16px;font-weight:700;}
#closeBtn:hover{background:rgba(255,255,255,0.30);}
QLineEdit{background:@{s1};border:1px solid rgba(255,255,255,0.10);border-radius:12px;
  min-height:38px;padding:0 12px;color:@{tp};selection-background-color:@{ac};}
QLineEdit:focus{border:1px solid @{ac};}
#row{background:transparent;border-radius:10px;}
#row:hover{background:@{s2};}
#nm{color:@{tp};font-size:14px;font-weight:600;}
#kind{color:@{tt};font-size:12px;}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:@{bStr};border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
)QSS")));

    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    auto* head = new QWidget();
    head->setObjectName(QStringLiteral("dlgHeader"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setFixedHeight(52);
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(20, 0, 12, 0);
    auto* t = new QLabel(title); t->setObjectName(QStringLiteral("dlgTitle"));
    auto* close = new QPushButton(QStringLiteral("✕")); close->setObjectName(QStringLiteral("closeBtn"));
    close->setCursor(Qt::PointingHandCursor); close->setFixedSize(32, 32);
    connect(close, &QPushButton::clicked, this, &ModalOverlay::closeAnimated);
    hh->addWidget(t); hh->addStretch(); hh->addWidget(close);
    lay->addWidget(head);

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(14, 12, 14, 14);
    v->setSpacing(10);
    auto* search = new QLineEdit();
    search->setPlaceholderText(QStringLiteral("Поиск чата…"));
    v->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, [this](const QString& s){ rebuild(s.trimmed()); });

    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listW = new QWidget();
    listBox_ = new QVBoxLayout(listW);
    listBox_->setContentsMargins(0, 0, 6, 0);
    listBox_->setSpacing(4);
    listBox_->addStretch();
    sa->setWidget(listW);
    v->addWidget(sa, 1);
    lay->addWidget(body, 1);

    rebuild(QString());
}

bool ChatPickerDialog::eventFilter(QObject* obj, QEvent* e) {
    if (e->type() == QEvent::MouseButtonRelease) {
        if (auto* w = qobject_cast<QWidget*>(obj)) {
            const QString id = w->property("pickId").toString();
            if (!id.isEmpty()) {
                for (const Chat& c : chats_) if (c.id == id) { emit picked(c); break; }
                closeAnimated();
                return true;
            }
        }
    }
    return ModalOverlay::eventFilter(obj, e);
}

void ChatPickerDialog::rebuild(const QString& filter) {
    while (listBox_->count() > 1) {
        QLayoutItem* it = listBox_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const QString f = filter.toLower();
    int row = 0;
    for (const Chat& c : chats_) {
        const QString title = c.isSaved ? QStringLiteral("Избранное") : c.displayName;
        if (!f.isEmpty() && !title.toLower().contains(f)) continue;
        auto* w = new QFrame(); w->setObjectName(QStringLiteral("row"));
        w->setProperty("pickId", c.id);
        w->installEventFilter(this);
        auto* h = new QHBoxLayout(w); h->setContentsMargins(8, 5, 10, 5); h->setSpacing(10);
        auto* av = new QLabel(); av->setFixedSize(40, 40);
        Avatar::setRound(av, c.isSaved ? QString() : c.avatarUrl, c.isSaved ? QStringLiteral("★") : title, 40);
        h->addWidget(av);
        auto* col = new QVBoxLayout(); col->setSpacing(1);
        auto* nm = new QLabel(title); nm->setObjectName(QStringLiteral("nm"));
        col->addWidget(nm);
        const QString kindTxt = c.kind == ChatKind::Group ? QStringLiteral("Группа")
                              : c.kind == ChatKind::Channel ? QStringLiteral("Канал")
                              : QString();
        if (!kindTxt.isEmpty()) { auto* k = new QLabel(kindTxt); k->setObjectName(QStringLiteral("kind")); col->addWidget(k); }
        h->addLayout(col, 1);
        listBox_->insertWidget(row++, w);
    }
}
