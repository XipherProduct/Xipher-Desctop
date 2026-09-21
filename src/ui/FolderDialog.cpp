#include "ui/FolderDialog.h"
#include "ui/AvatarUtil.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QScrollArea>
#include <QFrame>
#include <QUuid>

FolderEditorDialog::FolderEditorDialog(const QList<Chat>& chats, const Folder& existing, QWidget* parent)
    : ModalOverlay(parent, 460), chats_(chats), folder_(existing),
      isNew_(existing.id.isEmpty()) {
    card()->setFixedHeight(640);
    if (isNew_) folder_.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const QString& k : folder_.chatKeys) selected_.insert(k);

    card()->setStyleSheet(QStringLiteral(R"QSS(
#modalCard{background:#17151E;border:1px solid rgba(255,255,255,0.08);border-radius:18px;}
QLabel{color:#F3F1F8;}
#dlgHeader{background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #6D28D9,stop:0.55 #8B5CF6,stop:1 #B06CF0);
  border-top-left-radius:18px;border-top-right-radius:18px;}
#dlgTitle{font-size:17px;font-weight:800;color:#fff;}
#closeBtn{background:rgba(255,255,255,0.16);border:none;border-radius:16px;color:#fff;font-size:16px;font-weight:700;}
#closeBtn:hover{background:rgba(255,255,255,0.30);}
QLineEdit{background:#131218;border:1px solid rgba(255,255,255,0.10);border-radius:10px;
  min-height:36px;padding:0 12px;color:#F3F1F8;selection-background-color:#8B5CF6;}
QLineEdit:focus{border:1px solid #8B5CF6;}
#rowName{color:#CFC9DC;font-size:13px;}
#primaryBtn{background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8B5CF6,stop:1 #6D28D9);
  color:#fff;border:none;border-radius:10px;min-height:38px;padding:0 22px;font-weight:700;}
#primaryBtn:hover{background:#9B72F8;}
#delBtn{background:transparent;color:#E5687A;border:1px solid rgba(229,104,122,0.4);
  border-radius:10px;min-height:36px;padding:0 16px;}
#delBtn:hover{background:rgba(229,104,122,0.12);}
#chatRow{background:#1A1822;border-radius:10px;}
#chatRow:hover{background:#221F2C;}
QCheckBox{color:#F3F1F8;font-size:14px;spacing:10px;}
QCheckBox::indicator{width:18px;height:18px;border-radius:5px;border:1.5px solid #5A5470;background:transparent;}
QCheckBox::indicator:checked{background:#8B5CF6;border:1.5px solid #8B5CF6;}
QScrollArea{background:transparent;border:none;}
QScrollBar:vertical{background:transparent;width:8px;margin:2px;}
QScrollBar::handle:vertical{background:rgba(255,255,255,0.12);border-radius:4px;min-height:36px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}
)QSS"));

    auto* lay = cardLayout();
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // Шапка.
    auto* head = new QWidget();
    head->setObjectName(QStringLiteral("dlgHeader"));
    head->setAttribute(Qt::WA_StyledBackground, true);
    head->setFixedHeight(52);
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(20, 0, 12, 0);
    auto* title = new QLabel(isNew_ ? QStringLiteral("Новая папка") : QStringLiteral("Редактировать папку"));
    title->setObjectName(QStringLiteral("dlgTitle"));
    auto* close = new QPushButton(QStringLiteral("✕"));
    close->setObjectName(QStringLiteral("closeBtn"));
    close->setCursor(Qt::PointingHandCursor); close->setFixedSize(32, 32);
    connect(close, &QPushButton::clicked, this, &ModalOverlay::closeAnimated);
    hh->addWidget(title); hh->addStretch(); hh->addWidget(close);
    lay->addWidget(head);

    auto* body = new QWidget();
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);

    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText(QStringLiteral("Название папки"));
    nameEdit_->setMaxLength(64);
    nameEdit_->setText(folder_.name);
    v->addWidget(nameEdit_);

    // Иконка и цвет — как в редакторе папок веба: сетка глифов + палитра.
    if (folder_.icon.isEmpty())  folder_.icon  = QStringLiteral("chat");
    if (folder_.color.isEmpty()) folder_.color = FolderIcons::colors().first();
    auto* iconLbl = new QLabel(QStringLiteral("Иконка"));
    iconLbl->setObjectName(QStringLiteral("rowName"));
    v->addWidget(iconLbl);
    auto* iconGrid = new QWidget();
    auto* igl = new QGridLayout(iconGrid);
    igl->setContentsMargins(0, 0, 0, 0);
    igl->setSpacing(4);
    const QStringList iconKeys = FolderIcons::keys();
    QList<QPushButton*> iconBtns;
    for (int i = 0; i < iconKeys.size(); ++i) {
        auto* ib = new QPushButton(FolderIcons::glyph(iconKeys[i]));
        ib->setCursor(Qt::PointingHandCursor);
        ib->setFixedSize(34, 34);
        ib->setToolTip(iconKeys[i]);
        const QString key = iconKeys[i];
        const bool sel = (key == folder_.icon);
        ib->setStyleSheet(QStringLiteral(
            "QPushButton{border:1px solid %1;background:%2;border-radius:9px;"
            "font-size:16px;padding:0;}"
            "QPushButton:hover{border:1px solid rgba(139,92,246,0.6);}")
            .arg(sel ? QStringLiteral("rgba(139,92,246,0.9)") : QStringLiteral("transparent"))
            .arg(sel ? QStringLiteral("rgba(139,92,246,0.16)") : QStringLiteral("transparent")));
        connect(ib, &QPushButton::clicked, this, [this, ib, key, iconGrid]() {
            folder_.icon = key;
            // Переподсветить выбранную плитку.
            const auto btns = iconGrid->findChildren<QPushButton*>();
            for (QPushButton* b : btns)
                b->setStyleSheet(QStringLiteral(
                    "QPushButton{border:1px solid transparent;background:transparent;"
                    "border-radius:9px;font-size:16px;padding:0;}"
                    "QPushButton:hover{border:1px solid rgba(139,92,246,0.6);}"));
            ib->setStyleSheet(QStringLiteral(
                "QPushButton{border:1px solid rgba(139,92,246,0.9);"
                "background:rgba(139,92,246,0.16);border-radius:9px;font-size:16px;padding:0;}"
                "QPushButton:hover{border:1px solid rgba(139,92,246,0.6);}"));
        });
        iconBtns.append(ib);
        igl->addWidget(ib, i / 8, i % 8);
    }
    v->addWidget(iconGrid);

    auto* colorLbl = new QLabel(QStringLiteral("Цвет"));
    colorLbl->setObjectName(QStringLiteral("rowName"));
    v->addWidget(colorLbl);
    auto* colorRow = new QWidget();
    auto* crl = new QHBoxLayout(colorRow);
    crl->setContentsMargins(0, 0, 0, 0);
    crl->setSpacing(8);
    for (const QString& col : FolderIcons::colors()) {
        auto* cb = new QPushButton();
        cb->setCursor(Qt::PointingHandCursor);
        cb->setFixedSize(26, 26);
        cb->setToolTip(col);
        const bool sel = (col == folder_.color);
        cb->setStyleSheet(QStringLiteral(
            "QPushButton{border-radius:13px;background:%1;"
            "border:2px solid %2;padding:0;}")
            .arg(col, sel ? QStringLiteral("#F3F1F8") : QStringLiteral("transparent")));
        connect(cb, &QPushButton::clicked, this, [this, cb, col, colorRow]() {
            folder_.color = col;
            const auto btns = colorRow->findChildren<QPushButton*>();
            for (QPushButton* b : btns)
                b->setStyleSheet(b->styleSheet().replace(
                    QStringLiteral("border:2px solid #F3F1F8;"),
                    QStringLiteral("border:2px solid transparent;")));
            cb->setStyleSheet(cb->styleSheet().replace(
                QStringLiteral("border:2px solid transparent;"),
                QStringLiteral("border:2px solid #F3F1F8;")));
        });
        crl->addWidget(cb);
    }
    crl->addStretch();
    v->addWidget(colorRow);

    auto* search = new QLineEdit();
    search->setPlaceholderText(QStringLiteral("Поиск чатов…"));
    v->addWidget(search);
    connect(search, &QLineEdit::textChanged, this, [this](const QString& t){ rebuildList(t.trimmed()); });

    auto* sa = new QScrollArea();
    sa->setWidgetResizable(true);
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* listW = new QWidget();
    listBox_ = new QVBoxLayout(listW);
    listBox_->setContentsMargins(0, 0, 6, 0);
    listBox_->setSpacing(6);
    listBox_->addStretch();
    sa->setWidget(listW);
    v->addWidget(sa, 1);

    // Кнопки.
    auto* btnRow = new QHBoxLayout();
    if (!isNew_) {
        auto* del = new QPushButton(QStringLiteral("Удалить"));
        del->setObjectName(QStringLiteral("delBtn"));
        del->setCursor(Qt::PointingHandCursor);
        connect(del, &QPushButton::clicked, this, [this]() { emit removed(folder_.id); closeAnimated(); });
        btnRow->addWidget(del);
    }
    btnRow->addStretch();
    auto* save = new QPushButton(QStringLiteral("Сохранить"));
    save->setObjectName(QStringLiteral("primaryBtn"));
    save->setCursor(Qt::PointingHandCursor);
    connect(save, &QPushButton::clicked, this, [this]() {
        const QString nm = nameEdit_->text().trimmed();
        if (nm.isEmpty()) { nameEdit_->setFocus(); return; }
        folder_.name = nm;
        folder_.chatKeys = QStringList(selected_.values());
        emit saved(folder_);
        closeAnimated();
    });
    btnRow->addWidget(save);
    v->addLayout(btnRow);

    lay->addWidget(body, 1);

    rebuildList(QString());
}

void FolderEditorDialog::rebuildList(const QString& filter) {
    while (listBox_->count() > 1) {
        QLayoutItem* it = listBox_->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const QString f = filter.toLower();
    int row = 0;
    for (const Chat& c : chats_) {
        const QString title = c.isSaved ? QStringLiteral("Избранное")
                                        : (c.displayName.isEmpty() ? c.name : c.displayName);
        if (!f.isEmpty() && !title.toLower().contains(f)) continue;
        const QString key = chatKeyFor(c);

        auto* rowW = new QFrame();
        rowW->setObjectName(QStringLiteral("chatRow"));
        auto* h = new QHBoxLayout(rowW);
        h->setContentsMargins(10, 6, 12, 6);
        h->setSpacing(10);

        auto* av = new QLabel(); av->setFixedSize(36, 36);
        Avatar::setRound(av, c.isSaved ? QString() : c.avatarUrl,
                         c.isSaved ? QStringLiteral("★") : title, 36);
        h->addWidget(av);

        auto* cb = new QCheckBox(title);
        cb->setChecked(selected_.contains(key));
        connect(cb, &QCheckBox::toggled, this, [this, key](bool on){
            if (on) selected_.insert(key); else selected_.remove(key);
        });
        h->addWidget(cb, 1);

        listBox_->insertWidget(row++, rowW);
    }
}
